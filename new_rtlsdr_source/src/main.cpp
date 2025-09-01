#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/smgui.h>
#include <rtl-sdr.h>


#ifdef __ANDROID__
#include <android_backend.h>
#endif

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "new_rtlsdr_source",
    /* Description:     */ "NEW-RTL-SDR source module for SDR++",
    /* Author:          */ "sultan_papagani",
    /* Version:         */ 0, 0, 1,
    /* Max instances    */ 1
};

ConfigManager config;

const double sampleRates[] = {
    250000,
    1024000,
    1536000,
    1792000,
    1920000,
    2048000,
    2160000,
    2400000,
    2560000,
    2880000,
    3200000
};

const char* sampleRatesTxt[] = {
    "250KHz",
    "1.024MHz",
    "1.536MHz",
    "1.792MHz",
    "1.92MHz",
    "2.048MHz",
    "2.16MHz",
    "2.4MHz",
    "2.56MHz",
    "2.88MHz",
    "3.2MHz"
};

//const char* channelFilQTxt = "High Q\0Low Q";

//const char* agcPinTxt = "agc_in\0agc_in2(R828D)";

//const char* vgaPowerLevelTxt = "Max Power\0Min Power";

//const char* mixerBufferCurrentTxt = "High Current\0Low Current\0";

//const char* mixerCurrentControlTxt = "Max Current\0Normal Current\0";

//const char* filtBandwithManualTxt = "Widest\0Middle\0Narrowest\0";

//const char* mixerInputSourceTxt = "Rf In\0Ring PLL\0";

//const char* trackingFilterTxt = "On\0Bypass\0";

const char* agcModesTxt = "Hardware\0Software\0";

const char* sidebandTxt = "Lower Side\0Upper Side\0";

const char* directSamplingModesTxt = "Disabled\0I branch\0Q branch\0";

const char* agcClockTxt = "300ms\0 80ms\0 20ms\0";

//const char* rfFilterRejectTxt = "Highest Band\0 Med Band\0 Low Band\0";

class RTLSDRSourceModule : public ModuleManager::Instance {
public:
    RTLSDRSourceModule(std::string name) {
        this->name = name;

        serverMode = (bool)core::args["server"];

        sampleRate = sampleRates[0];

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        strcpy(dbTxt, "--");
        strcpy(lnaGainTxt, "0");
        strcpy(vgaGainTxt, "0");
        strcpy(mixerGainTxt, "0");

        //strcpy(lnaAgcPdetHigh, "0.34V");
        //strcpy(lnaAgcPdetLow, "0.34V");
        //strcpy(mixerAgcPdetHigh, "0.34V");
        //strcpy(mixerAgcPdetLow, "0.34V");
        

        for (int i = 0; i < 11; i++) {
            sampleRateListTxt += sampleRatesTxt[i];
            sampleRateListTxt += '\0';
        }

        refresh();

        config.acquire();
        if (!config.conf["device"].is_string()) {
            selectedDevName = "";
            config.conf["device"] = "";
        }
        else {
            selectedDevName = config.conf["device"];
        }
        config.release(true);
        selectByName(selectedDevName);

        sigpath::sourceManager.registerSource("NEW-RTL-SDR", &handler);
    }

    ~RTLSDRSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("NEW-RTL-SDR");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {
        devNames.clear();
        devListTxt = "";

#ifndef __ANDROID__
        devCount = rtlsdr_get_device_count();
        char buf[1024];
        char snBuf[1024];
        for (int i = 0; i < devCount; i++) {
            // Gather device info
            const char* devName = rtlsdr_get_device_name(i);
            int snErr = rtlsdr_get_device_usb_strings(i, NULL, NULL, snBuf);

            // Build name
            sprintf(buf, "[%s] %s##%d", (!snErr && snBuf[0]) ? snBuf : "No Serial", devName, i);
            devNames.push_back(buf);
            devListTxt += buf;
            devListTxt += '\0';
        }
#else
        // Check for device connection
        devCount = 0;
        int vid, pid;
        devFd = backend::getDeviceFD(vid, pid, backend::RTL_SDR_VIDPIDS);
        if (devFd < 0) { return; }

        // Generate fake device info
        devCount = 1;
        std::string fakeName = "RTL-SDR Dongle USB";
        devNames.push_back(fakeName);
        devListTxt += fakeName;
        devListTxt += '\0';
#endif
    }

    void selectFirst() {
        if (devCount > 0) {
            selectById(0);
        }
    }

    void selectByName(std::string name) {
        for (int i = 0; i < devCount; i++) {
            if (name == devNames[i]) {
                selectById(i);
                return;
            }
        }
        selectFirst();
    }

    void selectById(int id) {
        selectedDevName = devNames[id];

#ifndef __ANDROID__
        int oret = rtlsdr_open(&openDev, id);
#else
        int oret = rtlsdr_open_sys_dev(&openDev, devFd);
#endif
        
        if (oret < 0) {
            selectedDevName = "";
            flog::error("Could not open RTL-SDR: {0}", oret);
            return;
        }

        gainList.clear();
        int gains[256];
        int n = rtlsdr_get_tuner_gains(openDev, gains);

        gainList = std::vector<int>(gains, gains + n);
        std::sort(gainList.begin(), gainList.end());

        // I HATE DEHYDRATED PISS YELLOW COLOR
        ImGuiStyle* style = &ImGui::GetStyle();
        style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.239f, 0.521f, 0.878f, 1.0f);

        io = &ImGui::GetIO();

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(selectedDevName)) {
            created = true;
            config.conf["devices"][selectedDevName]["sampleRate"] = 2400000.0;
            config.conf["devices"][selectedDevName]["directSampling"] = directSamplingMode;
            config.conf["devices"][selectedDevName]["ppm"] = 0;
            config.conf["devices"][selectedDevName]["biasT"] = biasT;
            config.conf["devices"][selectedDevName]["offsetTuning"] = offsetTuning;
            config.conf["devices"][selectedDevName]["rtlAgc"] = rtlAgc;
            //config.conf["devices"][selectedDevName]["tunerAgc"] = tunerAgc;
            config.conf["devices"][selectedDevName]["gain"] = gainId;
        }
        updateGainTxt();

        // Load config
        if (config.conf["devices"][selectedDevName].contains("sampleRate")) {
            int selectedSr = config.conf["devices"][selectedDevName]["sampleRate"];
            for (int i = 0; i < 11; i++) {
                if (sampleRates[i] == selectedSr) {
                    srId = i;
                    sampleRate = selectedSr;
                    break;
                }
            }
        }

        if (config.conf["devices"][selectedDevName].contains("directSampling")) {
            //directSamplingMode = config.conf["devices"][selectedDevName]["directSampling"];
            directSamplingMode = false;
        }

        if (config.conf["devices"][selectedDevName].contains("ppm")) {
            ppm = config.conf["devices"][selectedDevName]["ppm"];
        }

        if (config.conf["devices"][selectedDevName].contains("biasT")) {
            biasT = config.conf["devices"][selectedDevName]["biasT"];
        }

        if (config.conf["devices"][selectedDevName].contains("offsetTuning")) {
            offsetTuning = config.conf["devices"][selectedDevName]["offsetTuning"];
        }

        if (config.conf["devices"][selectedDevName].contains("rtlAgc")) {
            rtlAgc = config.conf["devices"][selectedDevName]["rtlAgc"];
        }

        if (config.conf["devices"][selectedDevName].contains("tunerAgc")) {
            //tunerAgc = config.conf["devices"][selectedDevName]["tunerAgc"];
        }

        if (config.conf["devices"][selectedDevName].contains("gain")) {
            gainId = config.conf["devices"][selectedDevName]["gain"];
            updateGainTxt();
        }

        config.release(created);

        rtlsdr_close(openDev);
    }

private:
    std::string getBandwdithScaled(double bw) {
        char buf[1024];
        if (bw >= 1000000.0) {
            sprintf(buf, "%.1lfMHz", bw / 1000000.0);
        }
        else if (bw >= 1000.0) {
            sprintf(buf, "%.1lfKHz", bw / 1000.0);
        }
        else {
            sprintf(buf, "%.1lfHz", bw);
        }
        return std::string(buf);
    }

    static void menuSelected(void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("RTLSDRSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        flog::info("RTLSDRSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        if (_this->running) { return; }
        if (_this->selectedDevName == "") {
            flog::error("No device selected");
            return;
        }

#ifndef __ANDROID__
        int oret = rtlsdr_open(&_this->openDev, _this->devId);
#else
        int oret = rtlsdr_open_sys_dev(&_this->openDev, _this->devFd);
#endif

        if (oret < 0) {
            flog::error("Could not open RTL-SDR");
            return;
        }

        flog::info("RTL-SDR Sample Rate: {0}", _this->sampleRate);

        rtlsdr_set_sample_rate(_this->openDev, _this->sampleRate);
        rtlsdr_set_center_freq(_this->openDev, _this->freq);
        rtlsdr_set_freq_correction(_this->openDev, _this->ppm);
        rtlsdr_set_tuner_bandwidth(_this->openDev, 0);
        rtlsdr_set_direct_sampling(_this->openDev, _this->directSamplingMode);
        rtlsdr_set_bias_tee(_this->openDev, _this->biasT);
        rtlsdr_set_agc_mode(_this->openDev, _this->rtlAgc);
        rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
        
        rtlsdr_set_tuner_gain_mode(_this->openDev, 1); //manual mode default
        rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);

        rtlsdr_set_offset_tuning(_this->openDev, _this->offsetTuning);

        rtlsdr_tuner tuner_type = rtlsdr_get_tuner_type(_this->openDev);
        if (tuner_type == RTLSDR_TUNER_R820T || tuner_type == RTLSDR_TUNER_R828D)
        {
            if (tuner_type == RTLSDR_TUNER_R828D){_this->showIQ = false;}
        }
        else{_this->correctTuner = false;}

        _this->asyncCount = (int)roundf(_this->sampleRate / (200 * 512)) * 512;

        _this->workerThread = std::thread(&RTLSDRSourceModule::worker, _this);

        _this->running = true;
        flog::info("RTLSDRSourceModule '{0}': Start!", _this->name);
    }

    static void stop(void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->stream.stopWriter();
        rtlsdr_cancel_async(_this->openDev);
        if (_this->workerThread.joinable()) { _this->workerThread.join(); }
        _this->stream.clearWriteStop();
        rtlsdr_close(_this->openDev);
        flog::info("RTLSDRSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        if (_this->running) {
            uint32_t newFreq = freq;
            int i;
            for (i = 0; i < 10; i++) {
                rtlsdr_set_center_freq(_this->openDev, freq);
                if (rtlsdr_get_center_freq(_this->openDev) == newFreq) { break; }
            }
            if (i > 1) {
                flog::warn("RTL-SDR took {0} attempts to tune...", i);
            }
        }
        _this->freq = freq;
        flog::info("RTLSDRSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;

        if (!_this->correctTuner)
        {
            ImGui::Text("wrong tuner!.");
            return;
        }

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_rtlsdr_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectById(_this->devId);
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["device"] = _this->selectedDevName;
                config.release(true);
            }
        }

        if (SmGui::Combo(CONCAT("##_rtlsdr_sr_sel_", _this->name), &_this->srId, _this->sampleRateListTxt.c_str())) {
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["sampleRate"] = _this->sampleRate;
                config.release(true);
            }
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_rtlsdr_refr_", _this->name)/*, ImVec2(refreshBtnWdith, 0)*/)) {
            _this->refresh();
            _this->selectByName(_this->selectedDevName);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        // Rest of rtlsdr config here

        if(_this->showIQ){

        SmGui::LeftLabel("Direct Sampling");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rtlsdr_ds_", _this->name), &_this->directSamplingMode, directSamplingModesTxt)) {
            if (_this->running) {
                rtlsdr_set_direct_sampling(_this->openDev, _this->directSamplingMode);

                // Update gains (fix for librtlsdr bug)
                
                if (_this->directSamplingMode == false) {

                    rtlsdr_set_direct_sampling(_this->openDev, 1);
                    rtlsdr_set_direct_sampling(_this->openDev, 0);
                    rtlsdr_set_agc_mode(_this->openDev, _this->rtlAgc);

                    if(_this->controlMode == 2)
                    {
                        int mode = 0;
                        if (_this->agcModeId == 1){mode = 2;}
                        rtlsdr_set_tuner_gain_mode(_this->openDev, mode);
                    }
                    else
                    {
                        rtlsdr_set_tuner_gain_mode(_this->openDev, 1);
                    }
                    rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
                }
                
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["directSampling"] = _this->directSamplingMode;
                config.release(true);
            }
        }
        }

        SmGui::LeftLabel("PPM Correction");
        SmGui::FillWidth();
        if (SmGui::InputInt(CONCAT("##_rtlsdr_ppm_", _this->name), &_this->ppm, 1, 10)) {
            _this->ppm = std::clamp<int>(_this->ppm, -1000000, 1000000);
            if (_this->running) {
                rtlsdr_set_freq_correction(_this->openDev, _this->ppm);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["ppm"] = _this->ppm;
                config.release(true);
            }
        }

        // -------------------------------------------

        if (!_this->running){SmGui::BeginDisabled();}

        if (!_this->directSamplingMode){

        SmGui::Text("Tuner IF Frequency");
        if (SmGui::InputInt(CONCAT("##_rtlsdr_iffreq", _this->name), &_this->if_freq_tuner,0)){rtlsdr_set_if_freq(_this->openDev, _this->if_freq_tuner);}
        SmGui::SameLine();
        if (SmGui::Button(CONCAT("Reset##_rtlsdr_ifreset", _this->name))){_this->if_freq_tuner = 3570000;rtlsdr_set_if_freq(_this->openDev, _this->if_freq_tuner);};

        }

        SmGui::LeftLabel("Tuner Sideband");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rtlsdr_sideband_", _this->name), &_this->sideband, sidebandTxt))
        {
            if (_this->running)
            {
                rtlsdr_set_tuner_sideband(_this->openDev, _this->sideband);
            }
        }

        if (_this->showGains)
        {
            if(_this->running)
            {
                int current_dagc = rtlsdr_get_dagc_gain(_this->openDev);

                float delta = (float)current_dagc - _this->rtl_dagc;
                delta *= _this->io->DeltaTime * _this->tween_speed;
                _this->rtl_dagc += delta;
                

                int current_strength = 0;
                rtlsdr_get_tuner_i2c_register(_this->openDev, _this->tuner_register_read, &_this->len, &current_strength);

                char mixerg = (_this->tuner_register_read[3] & 0xF0) >> 4;
                char lnag = (_this->tuner_register_read[3] & 0x0F);

                delta = (float)mixerg - _this->mixerGainRead;
                delta *= _this->io->DeltaTime * _this->tween_speed;
                _this->mixerGainRead += delta;

                delta = (float)lnag - _this->lnaGainRead;
                delta *= _this->io->DeltaTime * _this->tween_speed;
                _this->lnaGainRead += delta;

                delta = (float)current_strength - _this->strength;
                delta *= _this->io->DeltaTime * _this->tween_speed;
                _this->strength += delta;
            }

            ImGui::FillWidth();
            ImGui::ProgressBar(_this->rtl_dagc/255.0f, ImVec2(0.0,20.0), "RTL Gain");

            ImGui::FillWidth();
            ImGui::ProgressBar(_this->mixerGainRead/15.0f, ImVec2(0.0,20.0), "Mixer Gain"); 

            ImGui::FillWidth();
            ImGui::ProgressBar(_this->lnaGainRead/15.0f, ImVec2(0.0,20.0), "Lna Gain"); 

            ImGui::FillWidth();
            ImGui::ProgressBar((float)_this->strength/947.0f, ImVec2(0.0,20.0), "Total Gain"); 
        }

        SmGui::BeginGroup();
        SmGui::Columns(3, CONCAT("RtlSdrModeColumns##_", _this->name), false);
        SmGui::ForceSync();

        if (SmGui::RadioButton(CONCAT("Basic##_rtl_gm_", _this->name), _this->controlMode == 0)) {
            _this->controlMode = 0;
            rtlsdr_set_tuner_gain_mode(_this->openDev, 1); // manual mod
            rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]); // bug fix 

            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x05, 0x10, 0x00); // lna auto gain
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 0x10, 0x10); // mixer auto gain
        }

        SmGui::NextColumn();
        SmGui::ForceSync();

        if (SmGui::RadioButton(CONCAT("Manual##_rtl_gm_", _this->name), _this->controlMode == 1)) {
            _this->controlMode = 1;
            rtlsdr_set_tuner_gain_mode(_this->openDev, 1); // manual mod
            rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]); // bug fix 

        

            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x05, 0x10, 0x10); // lna manual gain
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 0x10, 0x00); // mixer manual gain

            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x05, 0x0F, _this->lnaGain);
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 0x0F, _this->mixerGain);
            rtlsdr_set_tuner_gain_index(_this->openDev, _this->vgaGain);
            
        }

        SmGui::NextColumn();
        SmGui::ForceSync();

        if (SmGui::RadioButton(CONCAT("AGC##_rtl_gm_", _this->name), _this->controlMode == 2)) {
            _this->controlMode = 2;

            rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]); // bug fix 

            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x05, 0x10, 0x00); // lna auto gain
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 0x10, 0x10); // mixer auto gain

            
            if (_this->agcModeId == 0) // hardware
            {
                rtlsdr_set_tuner_gain_mode(_this->openDev, 0);
            }
            else // software
            {
                rtlsdr_set_tuner_gain_mode(_this->openDev, 2);
            }
        }

        SmGui::Columns(1, CONCAT("EndRtlSdrModeColumns##_", _this->name), false);
        SmGui::EndGroup();

        #pragma region "Control Modes"

        if (_this->controlMode == 0)
        {

            SmGui::LeftLabel("Gain");
            SmGui::FillWidth();
            SmGui::ForceSync();

            if (ImGui::SliderInt(CONCAT("##_rtlsdr_gain_", _this->name), &_this->gainId, 0, _this->gainList.size() - 1, _this->dbTxt)) {
            _this->updateGainTxt();
            if (_this->running) {
                rtlsdr_set_tuner_gain(_this->openDev, _this->gainList[_this->gainId]);
            }
            }
        }
        else if (_this->controlMode == 1)
        {
            SmGui::LeftLabel("Lna Gain");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_lnagain_", _this->name), &_this->lnaGain, 0, 15, _this->lnaGainTxt)) 
            {
                sprintf(_this->lnaGainTxt, "%i", _this->lnaGain);
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x05, 0x0F, _this->lnaGain);
            }

            SmGui::LeftLabel("Mixer Gain");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_mixergain_", _this->name), &_this->mixerGain, 0, 15, _this->mixerGainTxt)) 
            {
                sprintf(_this->mixerGainTxt, "%i", _this->mixerGain);
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 0x0F, _this->mixerGain);
            }

            SmGui::LeftLabel("Vga Gain");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_vgagain_", _this->name), &_this->vgaGain, 0, 15, _this->vgaGainTxt))
            {
                sprintf(_this->vgaGainTxt, "%.1f dB", -12.0 + (_this->vgaGain * 3.5));
                rtlsdr_set_tuner_gain_index(_this->openDev, _this->vgaGain);
            }

            // filters
            /*
            SmGui::LeftLabel("LPF Cutoff");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_lpfcut_", _this->name), &_this->lpfCutoff, 0, 15))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1B, 15 , 15 - _this->lpfCutoff);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("0-Lowest Corner 15-Highest Corner");
            }

            SmGui::LeftLabel("LPNF Cutoff");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_lpnfcut_", _this->name), &_this->lpnfCutoff, 0, 15))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1B, 240 , (15 - _this->lpnfCutoff) << 4);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("0-Lowest Corner 15-Highest Corner");
            }

            SmGui::LeftLabel("HPF Cutoff");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_hpfcut_", _this->name), &_this->hpfCutoff, 0, 15))
            { 
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0B, 15 , 15 - _this->hpfCutoff);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("0-Lowest 15-Highest");
            }
            */

        }
        else if (_this->controlMode == 2)
        {
            // AGC modu
            SmGui::LeftLabel("AGC Mode");
            SmGui::FillWidth();
            SmGui::ForceSync();
            if (SmGui::Combo(CONCAT("##_rtlsdr_agcmode_", _this->name), &_this->agcModeId, agcModesTxt))
            {
                if (_this->agcModeId == 0) // hardware
                {
                    rtlsdr_set_tuner_gain_mode(_this->openDev, 0);
                }
                else // software
                {
                    rtlsdr_set_tuner_gain_mode(_this->openDev, 2);
                }
            }
        }

        #pragma endregion


        //--

        // filters
        if (ImGui::CollapsingHeader(CONCAT("Filters##_rtlsdr_filheader", _this->name), ImGuiTreeNodeFlags_DefaultOpen)){

        SmGui::LeftLabel("Filter BW");
        SmGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_rtlsdr_filterbw_", _this->name), &_this->filterBw, 0, 15))
        {
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0A, 15, _this->filterBw);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("0-Widest 15-Narrowest");
        }

        SmGui::LeftLabel("LPF Cutoff");
        SmGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_rtlsdr_lpfcut_", _this->name), &_this->lpfCutoff, 0, 15))
        {
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1B, 15 , 15 - _this->lpfCutoff);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("0-Lowest Corner 15-Highest Corner");
        }

        SmGui::LeftLabel("LPNF Cutoff");
        SmGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_rtlsdr_lpnfcut_", _this->name), &_this->lpnfCutoff, 0, 15))
        {
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1B, 240 , (15 - _this->lpnfCutoff) << 4);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("0-Lowest Corner 15-Highest Corner");
        }

        SmGui::LeftLabel("HPF Cutoff");
        SmGui::FillWidth();
        if (ImGui::SliderInt(CONCAT("##_rtlsdr_hpfcut_", _this->name), &_this->hpfCutoff, 0, 15))
        { 
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0B, 15 , 15 - _this->hpfCutoff);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("0-Lowest 15-Highest");
        }

        }

        //--

        SmGui::LeftLabel("AGC Clock");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_rtlsdr_agclock_", _this->name), &_this->agcClockId, agcClockTxt)) 
        {
            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1A, 48, _this->agcClockId+1 << 4);
        }


        if (!_this->running) {SmGui::EndDisabled();}

        //------------------

        if (SmGui::Checkbox(CONCAT("Bias T##_rtlsdr_rtl_biast_", _this->name), &_this->biasT)) {
            if (_this->running) {
                rtlsdr_set_bias_tee(_this->openDev, _this->biasT);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["biasT"] = _this->biasT;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("Offset Tuning##_rtlsdr_rtl_ofs_", _this->name), &_this->offsetTuning)) {
            if (_this->running) {
                rtlsdr_set_offset_tuning(_this->openDev, _this->offsetTuning);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["offsetTuning"] = _this->offsetTuning;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("RTL AGC##_rtlsdr_rtl_agc_", _this->name), &_this->rtlAgc)) {
            if (_this->running) {
                rtlsdr_set_agc_mode(_this->openDev, _this->rtlAgc);
            }
            if (_this->selectedDevName != "") {
                config.acquire();
                config.conf["devices"][_this->selectedDevName]["rtlAgc"] = _this->rtlAgc;
                config.release(true);
            }
        }

        if (SmGui::Checkbox(CONCAT("Show Gains##_rtlsdr_showgains", _this->name), &_this->showGains));

        /*
        if (!_this->running) {SmGui::BeginDisabled();}

        ImGui::Checkbox(CONCAT("Show Tuner Controls##_rtlsdr_exp", _this->name), &_this->exposedTunerControl);
        if (_this->exposedTunerControl)
        {
            SmGui::LeftLabel("Reject 3rd Harmonic");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_rfreject_", _this->name), &_this->rfReject3rdId, rfFilterRejectTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1A, 3, _this->rfReject3rdId);
            }

            SmGui::LeftLabel("Tracking Filter");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_trackfil_", _this->name), &_this->trackFiltId, trackingFilterTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1A, 64, 64 * _this->trackFiltId);
            }

            if (SmGui::Checkbox(CONCAT("Tracking Fil. Q##rtlsdr_qenhanc", _this->name), &_this->trackFilQ))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x00, 128 , 128 * _this->trackFilQ);
            }

            SmGui::LeftLabel("Channel filter Q");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_chanfilq_", _this->name), &_this->channelFilQId, channelFilQTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x02, 64 , 64 * _this->channelFilQId);
            }


            SmGui::Text("--- LNA Pdet---");
            SmGui::NextColumn();

            SmGui::LeftLabel("NarrowBand TOP");
            SmGui::FillWidth();
            if (SmGui::SliderInt(CONCAT("##_rtlsdr_pdet2top", _this->name), &_this->pdet2TOP , 0, 7))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1D, 63 , (_this->pdet1TOP << 3)+_this->pdet2TOP);
            }

            SmGui::LeftLabel("WideBand TOP");
            SmGui::FillWidth();
            if (SmGui::SliderInt(CONCAT("##_rtlsdr_pdet1top", _this->name), &_this->pdet1TOP , 0, 7))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1D, 63 , (_this->pdet1TOP << 3)+_this->pdet2TOP);
            }

            SmGui::Text("Agc Thresholds");
            SmGui::LeftLabel("Low");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_lnaagclow", _this->name), &_this->lnaAgcPdetVoltageTreshLow , 0, 15, _this->lnaAgcPdetLow))
            {
                sprintf(_this->lnaAgcPdetLow, "~%.2fV",0.34f+(0.1f *  _this->lnaAgcPdetVoltageTreshLow));
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0D, 15, _this->lnaAgcPdetVoltageTreshLow);
            }

            SmGui::LeftLabel("High");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_lnaagchigh", _this->name), &_this->lnaAgcPdetVoltageTreshHigh , 0, 15, _this->lnaAgcPdetHigh))
            {
                sprintf(_this->lnaAgcPdetHigh, "~%.2fV", 0.34f+(0.1f * _this->lnaAgcPdetVoltageTreshHigh));
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0D, 240, _this->lnaAgcPdetVoltageTreshHigh << 4);
            }

            ImGui::NewLine();
            SmGui::Text("--- Mixer Pdet ---");

            SmGui::LeftLabel("TOP");
            SmGui::FillWidth();
            if (SmGui::SliderInt(CONCAT("##_rtlsdr_pdet3top", _this->name), &_this->pdet3TOP , 0, 15))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1C, 240 , _this->pdet3TOP << 4);
            }

            SmGui::Text("Agc Thresholds");
            SmGui::LeftLabel("Low");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_mixeragclow", _this->name), &_this->mixerAgcPdetVoltageTreshLow , 0, 15, _this->mixerAgcPdetLow))
            {
                sprintf(_this->mixerAgcPdetLow, "~%.2fV", 0.34f+(0.1f * _this->mixerAgcPdetVoltageTreshLow));
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0E, 15, _this->mixerAgcPdetVoltageTreshLow);
            }

            SmGui::LeftLabel("High");
            SmGui::FillWidth();
            if (ImGui::SliderInt(CONCAT("##_rtlsdr_mixeragchigh", _this->name), &_this->mixerAgcPdetVoltageTreshHigh , 0, 15, _this->mixerAgcPdetHigh))
            {
                sprintf(_this->mixerAgcPdetHigh, "~%.2fV", 0.34f+(0.1f * _this->mixerAgcPdetVoltageTreshHigh));
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0E, 240 , _this->mixerAgcPdetVoltageTreshHigh << 4);
            }

            ImGui::NewLine();
            SmGui::Text("---Experimental---");

            SmGui::LeftLabel("Mixer Current");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_mixercurcon_", _this->name), &_this->mixerCurrentControlId, mixerCurrentControlTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x07, 32 , 32 * _this->mixerCurrentControlId);
            }

            SmGui::LeftLabel("Mixer Buffer Current");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_mixerbufcur_", _this->name), &_this->mixerBufferCurrentId, mixerBufferCurrentTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x08, 64 , 64 * _this->mixerBufferCurrentId);
            }

            SmGui::LeftLabel("VGA Power");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_vgapowerlevel_", _this->name), &_this->vgaPowerLevelId, vgaPowerLevelTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0C, 32 , 32 * _this->vgaPowerLevelId);
            }

            SmGui::LeftLabel("AGC Pin");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_agcpinsel_", _this->name), &_this->agcPinId, agcPinTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x19, 16, 16 * _this->agcPinId);
            }

            SmGui::LeftLabel("Filt. Bandwith");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_filtbandmancor_", _this->name), &_this->filtBandwithManualId, filtBandwithManualTxt)) 
            {
                int value = _this->filtBandwithManualId;
                if (value == 2) {value = 7;} // turn 2 into b'111 (7)
	            rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0B, 224 , value << 5);
            }
            
            if (SmGui::Checkbox(CONCAT("Echo Compensation##_rtlsdr_echocomp_", _this->name), &_this->echo_compensation))
            SmGui::ForceSync();
            if (_this->echo_compensation)
            {
                SmGui::BeginGroup();
                SmGui::Columns(2, CONCAT("tunerecho##_te", _this->name), false);
                SmGui::ForceSync();

                if(SmGui::RadioButton(CONCAT("3db##_rtlsdr_ecm_", _this->name), _this->echo_compensationId == 0))
                {
                    _this->echo_compensationId = 0;
                    rtlsdr_set_tuner_i2c_register(_this->openDev, 0x02, 24 , (16 * _this->echo_compensation) + (8 * _this->echo_compensationId));
                }

                SmGui::NextColumn();
                SmGui::ForceSync();

                if(SmGui::RadioButton(CONCAT("1.5db##_rtlsdr_ecm_", _this->name), _this->echo_compensationId == 1))
                {
                    _this->echo_compensationId = 1;
                    rtlsdr_set_tuner_i2c_register(_this->openDev, 0x02, 24 , (16 * _this->echo_compensation) + (8 * _this->echo_compensationId));
                }

                SmGui::Columns(1, CONCAT("ENDtunerecho##_te", _this->name), false);
                SmGui::EndGroup();
            }

            
            SmGui::LeftLabel("Image Phase Adjust");
            SmGui::FillWidth();
            if (SmGui::SliderInt(CONCAT("##rtlsdr_imagephsadj_", _this->name), &_this->imagePhaseAdjust, 0, 31))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x09, 31 , _this->imagePhaseAdjust);
            }
            
            SmGui::LeftLabel("Image Gain Adjust");
            SmGui::FillWidth();
            if (SmGui::SliderInt(CONCAT("##rtlsdr_imagegadj_", _this->name), &_this->imageGainAdjust, 0, 31))
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x08, 31 , _this->imageGainAdjust);
            }

            SmGui::LeftLabel("Mixer input");
            SmGui::FillWidth();
            if (SmGui::Combo(CONCAT("##_rtlsdr_mixin_", _this->name), &_this->mixerInputSourceId, mixerInputSourceTxt)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x1C, 2, 2 * _this->mixerInputSourceId);
            }

            SmGui::LeftLabel("Filt. Extension Widest");
            SmGui::FillWidth();
            if (SmGui::Checkbox(CONCAT("##_rtlsdr_filtextwidest_", _this->name), &_this->filterExtensionWidest)) 
            {
                rtlsdr_set_tuner_i2c_register(_this->openDev, 0x0F, 128, 128 * _this->filterExtensionWidest);
            }
        }
        if (!_this->running) {SmGui::EndDisabled();}
        */
    }

    void worker() {
        rtlsdr_reset_buffer(openDev);
        rtlsdr_read_async(openDev, asyncHandler, this, 0, asyncCount);
    }

    static void asyncHandler(unsigned char* buf, uint32_t len, void* ctx) {
        RTLSDRSourceModule* _this = (RTLSDRSourceModule*)ctx;
        int sampCount = len / 2;
        for (int i = 0; i < sampCount; i++) {
            _this->stream.writeBuf[i].re = ((float)buf[i * 2] - 127.4) / 128.0f;
            _this->stream.writeBuf[i].im = ((float)buf[(i * 2) + 1] - 127.4) / 128.0f;
        }
        if (!_this->stream.swap(sampCount)) { return; }
    }

    void updateGainTxt() {
        if (gainId >= gainList.size()) { gainId = gainList.size() - 1; }
        sprintf(dbTxt, "%.1f dB", (float)gainList[gainId] / 10.0f);
    }

    std::string name;
    rtlsdr_dev_t* openDev;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    double sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedDevName = "";
    int devId = 0;
    int srId = 0;
    int devCount = 0;
    std::thread workerThread;
    bool serverMode = false;

#ifdef __ANDROID__
    int devFd = -1;
#endif

    int ppm = 0;

    bool biasT = false;

    int gainId = 0;
    std::vector<int> gainList;

    bool rtlAgc = false;

    ImGuiIO* io;

    //0 = Hardware 1 = Software
    int agcModeId = 0;

    //0: lower(Default) 1: Upper
    int sideband = 0;

    float lnaGainRead = 0;
    float mixerGainRead = 0;

    int vgaGain = 0;
    int lnaGain = 0;
    int mixerGain = 0;

    int len = 4;
    float strength = 0;
    unsigned char tuner_register_read[128];
    
    bool offsetTuning = false;
    int agcClockId = 1;

    /*
    int rfReject3rdId = 0;
    bool trackFilQ = false;
    int mixerCurrentControlId = 0;
    int mixerBufferCurrentId = 0;
    int vgaPowerLevelId = 0;
    int agcPinId = 0;
    int channelFilQId = 0;
    */

    float rtl_dagc = 0;
    int directSamplingMode = 0;
    bool showGains = true;

    /*
    int trackFiltId = 0;
    int mixerInputSourceId = 0;
    int filtBandwithManualId = 0;

    int imagePhaseAdjust = 0;
    int imageGainAdjust = 0;

    bool echo_compensation = false;
    int echo_compensationId = 0;

    bool exposedTunerControl = false;
    bool filterExtensionWidest = false;
    */

    int lpfCutoff = 0;
    int lpnfCutoff = 0;
    int hpfCutoff = 0;
    int filterBw = 0;

    int if_freq_tuner = 3570000;

    /*
    int lnaAgcPdetVoltageTreshHigh = 0;
    int lnaAgcPdetVoltageTreshLow = 0;

    int mixerAgcPdetVoltageTreshHigh = 0;
    int mixerAgcPdetVoltageTreshLow = 0;

    int lnaDischargeCurrent = 0;

    int pdet1TOP = 0; // lna wideband
    int pdet2TOP = 0; // lna narrowband
    int pdet3TOP = 0; // mixer

    int lnaNarrowbandDetectorBW = 0;

    bool lnaPdetWideband = false;
    bool lnaPdetNarrowband = false;

    */

    float tween_speed = 5.0f;

    /**
     * 0: Basic     (Only Gain Slider)
     * 1: Manual    
     * 2: AGC       (Tuner AGC or Software AGC)
    */
    int controlMode = 0;

    //0 = hardware agc, 1 = manual gain mode, 2 = software agc.
    int tunerAgc = 1;

    bool correctTuner = true;
    bool showIQ = true;


    // Handler stuff
    int asyncCount = 0;

    char dbTxt[128];
    char vgaGainTxt[20];
    char lnaGainTxt[20];
    char mixerGainTxt[20];

    //char lnaAgcPdetLow[20];
    //char lnaAgcPdetHigh[20];
    //char mixerAgcPdetLow[20];
    //char mixerAgcPdetHigh[20];


    std::vector<std::string> devNames;
    std::string devListTxt;
    std::string sampleRateListTxt;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = 0;
    config.setPath(core::args["root"].s() + "/rtl_sdr_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {

    return new RTLSDRSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (RTLSDRSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}
