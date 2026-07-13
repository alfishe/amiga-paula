#include "replayer.hpp"
#include <cstring>
#include <algorithm>

namespace mod {

const int16_t periodTable[16 * 37 + 15] = {
    856,808,762,720,678,640,604,570,538,508,480,453,
    428,404,381,360,339,320,302,285,269,254,240,226,
    214,202,190,180,170,160,151,143,135,127,120,113,0,
    850,802,757,715,674,637,601,567,535,505,477,450,
    425,401,379,357,337,318,300,284,268,253,239,225,
    213,201,189,179,169,159,150,142,134,126,119,113,0,
    844,796,752,709,670,632,597,563,532,502,474,447,
    422,398,376,355,335,316,298,282,266,251,237,224,
    211,199,188,177,167,158,149,141,133,125,118,112,0,
    838,791,746,704,665,628,592,559,528,498,470,444,
    419,395,373,352,332,314,296,280,264,249,235,222,
    209,198,187,176,166,157,148,140,132,125,118,111,0,
    832,785,741,699,660,623,588,555,524,495,467,441,
    416,392,370,350,330,312,294,278,262,247,233,220,
    208,196,185,175,165,156,147,139,131,124,117,110,0,
    826,779,736,694,655,619,584,551,520,491,463,437,
    413,390,368,347,328,309,292,276,260,245,232,219,
    206,195,184,174,164,155,146,138,130,123,116,109,0,
    820,774,730,689,651,614,580,547,516,487,460,434,
    410,387,365,345,325,307,290,274,258,244,230,217,
    205,193,183,172,163,154,145,137,129,122,115,109,0,
    814,768,725,684,646,610,575,543,513,484,457,431,
    407,384,363,342,323,305,288,272,256,242,228,216,
    204,192,181,171,161,152,144,136,128,121,114,108,0,
    907,856,808,762,720,678,640,604,570,538,508,480,
    453,428,404,381,360,339,320,302,285,269,254,240,
    226,214,202,190,180,170,160,151,143,135,127,120,0,
    900,850,802,757,715,675,636,601,567,535,505,477,
    450,425,401,379,357,337,318,300,284,268,253,238,
    225,212,200,189,179,169,159,150,142,134,126,119,0,
    894,844,796,752,709,670,632,597,563,532,502,474,
    447,422,398,376,355,335,316,298,282,266,251,237,
    223,211,199,188,177,167,158,149,141,133,125,118,0,
    887,838,791,746,704,665,628,592,559,528,498,470,
    444,419,395,373,352,332,314,296,280,264,249,235,
    222,209,198,187,176,166,157,148,140,132,125,118,0,
    881,832,785,741,699,660,623,588,555,524,494,467,
    441,416,392,370,350,330,312,294,278,262,247,233,
    220,208,196,185,175,165,156,147,139,131,123,117,0,
    875,826,779,736,694,655,619,584,551,520,491,463,
    437,413,390,368,347,328,309,292,276,260,245,232,
    219,206,195,184,174,164,155,146,138,130,123,116,0,
    868,820,774,730,689,651,614,580,547,516,487,460,
    434,410,387,365,345,325,307,290,274,258,244,230,
    217,205,193,183,172,163,154,145,137,129,122,115,0,
    862,814,768,725,684,646,610,575,543,513,484,457,
    431,407,384,363,342,323,305,288,272,256,242,228,
    216,203,192,181,171,161,152,144,136,128,121,114,0,
    774,1800,2314,3087,4113,4627,5400,6426,6940,7713,8739,9253,24625,12851,13365
};

const uint8_t vibratoTable[32] = {
    0x00, 0x18, 0x31, 0x4A, 0x61, 0x78, 0x8D, 0xA1,
    0xB4, 0xC5, 0xD4, 0xE0, 0xEB, 0xF4, 0xFA, 0xFD,
    0xFF, 0xFD, 0xFA, 0xF4, 0xEB, 0xE0, 0xD4, 0xC5,
    0xB4, 0xA1, 0x8D, 0x78, 0x61, 0x4A, 0x31, 0x18
};

const uint8_t funkTable[16] = {
    0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0D,
    0x10, 0x13, 0x16, 0x1A, 0x20, 0x2B, 0x40, 0x80
};

Replayer::Replayer() {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        channels[i].chanIndex = i;
        channels[i].dmaBit = 1 << i;
    }
}

void Replayer::setModule(Module* mod) {
    module = mod;
    speed = 6;
    bpm = 125;
    row = 0;
    position = 0;
    pattern = module->patternTable[0];
    tickCounter = 0;

    for (auto& ch : channels) {
        ch = Channel{};
        ch.chanIndex = &ch - channels.data();
        ch.dmaBit = 1 << ch.chanIndex;
    }
}

void Replayer::play() {
    if (!module) return;
    playing = true;
    tickCounter = speed;
}

void Replayer::stop() {
    playing = false;
    if (renderer) {
        renderer->writeWord(0xDFF096, 0x000F);
    }
}

double Replayer::ciaBpm2Hz(int b) const {
    if (b == 0) return 0.0;
    uint32_t ciaPeriod = 1773447 / b;
    return CIA_PAL_CLK / (ciaPeriod + 1);
}

void Replayer::tick() {
    if (!playing || !module) return;

    if (ciaSetBPM != -1) {
        bpm = ciaSetBPM;
        ciaSetBPM = -1;
    }

    tickCounter++;
    if (tickCounter >= speed) {
        tickCounter = 0;

        if (pattDelTime2 == 0) {
            dmaCon = 0;

            for (int i = 0; i < NUM_CHANNELS; i++) {
                playVoice(i);

                uint32_t addr = 0xDFF0A0 + (i * 16);
                renderer->writeWord(addr + 8, channels[i].volume);
            }

            setDMA();
        } else {
            for (int i = 0; i < NUM_CHANNELS; i++)
                checkEffects(i);
        }

        row++;

        if (pattDelTime > 0) {
            pattDelTime2 = pattDelTime;
            pattDelTime = 0;
        }

        if (pattDelTime2 > 0) {
            pattDelTime2--;
            if (pattDelTime2 > 0) row--;
        }

        if (pBreakFlag) {
            row = pBreakPosition;
            pBreakPosition = 0;
            pBreakFlag = false;
        }

        if (row >= MOD_ROWS || posJumpAssert) {
            row = pBreakPosition;
            pBreakPosition = 0;
            posJumpAssert = false;

            position++;
            if (position >= module->songLength) {
                position = 0;
            }
            pattern = module->patternTable[position];
            if (pattern >= module->numPatterns) pattern = 0;
        }
    } else {
        for (int i = 0; i < NUM_CHANNELS; i++)
            checkEffects(i);

        if (posJumpAssert) {
            row = pBreakPosition;
            pBreakPosition = 0;
            posJumpAssert = false;
            position++;
            if (position >= module->songLength) position = 0;
            pattern = module->patternTable[position];
        }
    }
}

void Replayer::playVoice(int ch) {
    auto& c = channels[ch];
    auto& note = module->patterns[pattern][row * NUM_CHANNELS + ch];

    // If no note/command, just update period register (like original PT)
    if (note.period == 0 && note.command == 0 && note.param == 0) {
        uint32_t addr = 0xDFF0A0 + (ch * 16);
        renderer->writeWord(addr + 6, c.period);
    }

    c.note = note.period;
    c.cmd = (note.command << 8) | note.param;

    if (note.sample >= 1 && note.sample <= MOD_SAMPLES) {
        c.sampleNum = note.sample - 1;
        auto& s = module->samples[c.sampleNum];

        c.sampleStart = &module->sampleData[s.offset];
        c.finetune = s.finetune & 0x0F;
        c.volume = s.volume;
        c.length = static_cast<uint16_t>(s.length >> 1);
        c.replen = static_cast<uint16_t>(s.loopLength >> 1);

        uint16_t repeat = static_cast<uint16_t>(s.loopStart >> 1);
        if (repeat > 0) {
            // Looping sample with loop start > 0
            c.loopStart = c.sampleStart + (repeat << 1);
            c.waveStart = c.loopStart;
            c.length = repeat + c.replen;
        } else {
            // Loop start is 0
            c.loopStart = c.sampleStart;
            c.waveStart = c.sampleStart;

            // For non-looping samples (loopLength <= 2), replen = 1
            // so it loops on the zeroed first word (silence)
            if (s.loopLength <= 2) {
                c.replen = 1;
            }
        }

        if (c.length == 0) {
            c.loopStart = c.waveStart = renderer->getNullSamplePtr();
            c.replen = 1;
        }
    }

    if ((c.note & 0xFFF) > 0) {
        if ((c.cmd & 0xFF0) == 0xE50) {
            setFineTune(ch);
            setPeriod(ch);
        } else {
            uint8_t cmd = (c.cmd & 0x0F00) >> 8;
            if (cmd == 3 || cmd == 5) {
                setTonePorta(ch);
                checkMoreEffects(ch);
            } else if (cmd == 9) {
                checkMoreEffects(ch);
                setPeriod(ch);
            } else {
                setPeriod(ch);
            }
        }
    } else {
        checkMoreEffects(ch);
    }
}

void Replayer::setPeriod(int ch) {
    auto& c = channels[ch];

    uint16_t note = c.note & 0xFFF;
    int i = 0;
    for (; i < 37; i++) {
        if (note >= periodTable[i]) break;
    }

    c.period = periodTable[(c.finetune * 37) + i];

    if ((c.cmd & 0xFF0) != 0xED0) {
        renderer->writeWord(0xDFF096, c.dmaBit);

        if ((c.waveControl & 0x04) == 0) c.vibratoPos = 0;
        if ((c.waveControl & 0x40) == 0) c.tremoloPos = 0;

        uint32_t addr = 0xDFF0A0 + (ch * 16);
        renderer->writeWord(addr + 4, c.length);
        renderer->writePtr(addr, c.sampleStart);

        if (!c.sampleStart) {
            c.loopStart = nullptr;
            renderer->writeWord(addr + 4, 1);
            c.replen = 1;
        }

        renderer->writeWord(addr + 6, c.period);
        dmaCon |= c.dmaBit;
    }

    checkMoreEffects(ch);
}

void Replayer::checkMoreEffects(int ch) {
    auto& c = channels[ch];
    uint8_t cmd = (c.cmd & 0x0F00) >> 8;

    switch (cmd) {
        case 0x9: sampleOffset(ch); return;
        case 0xB: positionJump(ch); return;
        case 0xD: patternBreak(ch); return;
        case 0xE: eCommands(ch); return;
        case 0xF: setSpeed(ch); return;
        case 0xC: volumeChange(ch); return;
    }

    uint32_t addr = 0xDFF0A0 + (ch * 16);
    renderer->writeWord(addr + 6, c.period);
}

void Replayer::checkEffects(int ch) {
    auto& c = channels[ch];
    updateFunk(ch);

    if ((c.cmd & 0xFFF) == 0) return;

    uint8_t cmd = (c.cmd & 0x0F00) >> 8;
    switch (cmd) {
        case 0x0: arpeggio(ch); return;
        case 0x1: portaUp(ch); return;
        case 0x2: portaDown(ch); return;
        case 0x3: tonePortamento(ch); return;
        case 0x4: vibrato(ch); return;
        case 0x5: tonePortNoChange(ch); volumeSlide(ch); break;
        case 0x6: vibrato2(ch); volumeSlide(ch); break;
        case 0xE: eCommands(ch); return;
        default: break;
    }

    uint32_t addr = 0xDFF0A0 + (ch * 16);
    renderer->writeWord(addr + 6, c.period);

    if (cmd == 0x7) {
        tremolo(ch);
        return;  // tremolo sets its own volume
    }

    if (cmd == 0xA) volumeSlide(ch);

    renderer->writeWord(addr + 8, c.volume);
}

void Replayer::setDMA() {
    renderer->writeWord(0xDFF096, 0x8000 | dmaCon);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        auto& c = channels[i];
        uint32_t addr = 0xDFF0A0 + (i * 16);
        renderer->writePtr(addr, c.loopStart);
        renderer->writeWord(addr + 4, c.replen);
    }
}

void Replayer::arpeggio(int ch) {
    auto& c = channels[ch];
    int arpTick = tickCounter % 3;
    int arpNote = 0;

    if (arpTick == 1) arpNote = c.cmd >> 4;
    else if (arpTick == 2) arpNote = c.cmd & 0xF;
    else {
        renderer->writeWord(0xDFF0A0 + (ch * 16) + 6, c.period);
        return;
    }

    const int16_t* periods = &periodTable[c.finetune * 37];
    for (int i = 0; i < 37; i++) {
        if (c.period >= periods[i]) {
            renderer->writeWord(0xDFF0A0 + (ch * 16) + 6, periods[i + arpNote]);
            break;
        }
    }
}

void Replayer::portaUp(int ch) {
    auto& c = channels[ch];
    c.period -= (c.cmd & 0xFF) & lowMask;
    lowMask = 0xFF;

    if ((c.period & 0xFFF) < 113)
        c.period = (c.period & 0xF000) | 113;

    renderer->writeWord(0xDFF0A0 + (ch * 16) + 6, c.period & 0xFFF);
}

void Replayer::portaDown(int ch) {
    auto& c = channels[ch];
    c.period += (c.cmd & 0xFF) & lowMask;
    lowMask = 0xFF;

    if ((c.period & 0xFFF) > 856)
        c.period = (c.period & 0xF000) | 856;

    renderer->writeWord(0xDFF0A0 + (ch * 16) + 6, c.period & 0xFFF);
}

void Replayer::setTonePorta(int ch) {
    auto& c = channels[ch];
    uint16_t note = c.note & 0xFFF;
    const int16_t* porta = &periodTable[c.finetune * 37];

    int i = 0;
    while (true) {
        if (note >= porta[i]) break;
        if (++i >= 37) { i = 35; break; }
    }

    if ((c.finetune & 8) && i > 0) i--;

    c.wantedPeriod = porta[i];
    c.tonePortDir = 0;

    if (c.period == c.wantedPeriod) c.wantedPeriod = 0;
    else if (c.period > c.wantedPeriod) c.tonePortDir = 1;
}

void Replayer::tonePortNoChange(int ch) {
    auto& c = channels[ch];
    if (c.wantedPeriod <= 0) return;

    if (c.tonePortDir > 0) {
        c.period -= c.tonePortSpeed;
        if (c.period <= c.wantedPeriod) {
            c.period = c.wantedPeriod;
            c.wantedPeriod = 0;
        }
    } else {
        c.period += c.tonePortSpeed;
        if (c.period >= c.wantedPeriod) {
            c.period = c.wantedPeriod;
            c.wantedPeriod = 0;
        }
    }

    uint32_t addr = 0xDFF0A0 + (ch * 16);
    if ((c.glissFunk & 0xF) == 0) {
        renderer->writeWord(addr + 6, c.period);
    } else {
        const int16_t* porta = &periodTable[c.finetune * 37];
        int i = 0;
        while (true) {
            if (c.period >= porta[i]) break;
            if (++i >= 37) { i = 35; break; }
        }
        renderer->writeWord(addr + 6, porta[i]);
    }
}

void Replayer::tonePortamento(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0xFF) > 0) {
        c.tonePortSpeed = c.cmd & 0xFF;
        c.cmd &= 0xFF00;
    }
    tonePortNoChange(ch);
}

void Replayer::vibrato2(int ch) {
    auto& c = channels[ch];
    uint8_t pos = (c.vibratoPos >> 2) & 0x1F;
    uint8_t type = c.waveControl & 3;
    uint16_t data;

    if (type == 0) data = vibratoTable[pos];
    else if (type == 1) {
        data = (c.vibratoPos < 128) ? (pos << 3) : (255 - (pos << 3));
    } else data = 255;

    data = (data * (c.vibratoCmd & 0xF)) >> 7;

    if (c.vibratoPos < 128) data = c.period + data;
    else data = c.period - data;

    renderer->writeWord(0xDFF0A0 + (ch * 16) + 6, data);
    c.vibratoPos += (c.vibratoCmd >> 2) & 0x3C;
}

void Replayer::vibrato(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0x0F) > 0)
        c.vibratoCmd = (c.vibratoCmd & 0xF0) | (c.cmd & 0x0F);
    if ((c.cmd & 0xF0) > 0)
        c.vibratoCmd = (c.cmd & 0xF0) | (c.vibratoCmd & 0x0F);
    vibrato2(ch);
}

void Replayer::tremolo(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0x0F) > 0)
        c.tremoloCmd = (c.tremoloCmd & 0xF0) | (c.cmd & 0x0F);
    if ((c.cmd & 0xF0) > 0)
        c.tremoloCmd = (c.cmd & 0xF0) | (c.tremoloCmd & 0x0F);

    uint8_t pos = (c.tremoloPos >> 2) & 0x1F;
    uint8_t type = (c.waveControl >> 4) & 3;
    int16_t data;

    if (type == 0) data = vibratoTable[pos];
    else if (type == 1) {
        data = (c.vibratoPos < 128) ? (pos << 3) : (255 - (pos << 3));
    } else data = 255;

    data = (static_cast<uint16_t>(data) * (c.tremoloCmd & 0xF)) >> 6;

    if (c.tremoloPos < 128) {
        data = c.volume + data;
        if (data > 64) data = 64;
    } else {
        data = c.volume - data;
        if (data < 0) data = 0;
    }

    renderer->writeWord(0xDFF0A0 + (ch * 16) + 8, data);
    c.tremoloPos += (c.tremoloCmd >> 2) & 0x3C;
}

void Replayer::volumeSlide(int ch) {
    auto& c = channels[ch];
    uint8_t param = c.cmd & 0xFF;

    if ((param & 0xF0) == 0) {
        c.volume -= param & 0x0F;
        if (c.volume < 0) c.volume = 0;
    } else {
        c.volume += param >> 4;
        if (c.volume > 64) c.volume = 64;
    }
}

void Replayer::sampleOffset(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0xFF) > 0)
        c.sampleOffset = c.cmd & 0xFF;

    uint16_t offset = c.sampleOffset << 7;
    if (offset < c.length) {
        c.length -= offset;
        c.sampleStart += offset << 1;
    } else {
        c.length = 1;
    }
}

void Replayer::volumeChange(int ch) {
    auto& c = channels[ch];
    c.volume = c.cmd & 0xFF;
    if (c.volume > 64) c.volume = 64;
}

void Replayer::positionJump(int ch) {
    auto& c = channels[ch];
    position = (c.cmd & 0xFF) - 1;
    pBreakPosition = 0;
    posJumpAssert = true;
}

void Replayer::patternBreak(int ch) {
    auto& c = channels[ch];
    pBreakPosition = (((c.cmd & 0xF0) >> 4) * 10) + (c.cmd & 0x0F);
    if (pBreakPosition > 63) pBreakPosition = 0;
    posJumpAssert = true;
}

void Replayer::setSpeed(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0xFF) > 0) {
        if ((c.cmd & 0xFF) < 32)
            speed = c.cmd & 0xFF;
        else
            ciaSetBPM = c.cmd & 0xFF;
    }
}

void Replayer::eCommands(int ch) {
    auto& c = channels[ch];
    uint8_t ecmd = (c.cmd & 0x00F0) >> 4;

    switch (ecmd) {
        case 0x0:
            renderer->writeByte(0xBFE001, ((c.cmd & 1) ^ 1) << 1);
            break;
        case 0x1: finePortaUp(ch); break;
        case 0x2: finePortaDown(ch); break;
        case 0x3: setGlissControl(ch); break;
        case 0x4: setVibratoControl(ch); break;
        case 0x5: setFineTune(ch); break;
        case 0x6: jumpLoop(ch); break;
        case 0x7: setTremoloControl(ch); break;
        case 0x9: retrigNote(ch); break;
        case 0xA: volumeFineUp(ch); break;
        case 0xB: volumeFineDown(ch); break;
        case 0xC: noteCut(ch); break;
        case 0xD: noteDelay(ch); break;
        case 0xE: patternDelay(ch); break;
    }
}

void Replayer::finePortaUp(int ch) {
    if (tickCounter == 0) {
        lowMask = 0x0F;
        portaUp(ch);
    }
}

void Replayer::finePortaDown(int ch) {
    if (tickCounter == 0) {
        lowMask = 0x0F;
        portaDown(ch);
    }
}

void Replayer::setGlissControl(int ch) {
    channels[ch].glissFunk = (channels[ch].glissFunk & 0xF0) | (channels[ch].cmd & 0x0F);
}

void Replayer::setVibratoControl(int ch) {
    channels[ch].waveControl = (channels[ch].waveControl & 0xF0) | (channels[ch].cmd & 0x0F);
}

void Replayer::setFineTune(int ch) {
    channels[ch].finetune = channels[ch].cmd & 0xF;
}

void Replayer::jumpLoop(int ch) {
    auto& c = channels[ch];
    if (tickCounter != 0) return;

    if ((c.cmd & 0xF) == 0) {
        c.pattPos = row;
    } else {
        if (c.loopCount == 0)
            c.loopCount = c.cmd & 0xF;
        else if (--c.loopCount == 0)
            return;

        pBreakPosition = c.pattPos;
        pBreakFlag = true;
    }
}

void Replayer::setTremoloControl(int ch) {
    channels[ch].waveControl = ((channels[ch].cmd & 0xF) << 4) | (channels[ch].waveControl & 0xF);
}

void Replayer::doRetrg(int ch) {
    auto& c = channels[ch];
    uint32_t addr = 0xDFF0A0 + (ch * 16);

    renderer->writeWord(0xDFF096, c.dmaBit);
    renderer->writePtr(addr, c.sampleStart);
    renderer->writeWord(addr + 4, c.length);
    renderer->writeWord(addr + 6, c.period);
    renderer->writeWord(0xDFF096, 0x8000 | c.dmaBit);
    renderer->writePtr(addr, c.loopStart);
    renderer->writeWord(addr + 4, c.replen);
}

void Replayer::retrigNote(int ch) {
    auto& c = channels[ch];
    if ((c.cmd & 0xF) > 0) {
        if (tickCounter == 0 && (c.note & 0xFFF) > 0) return;
        if (tickCounter % (c.cmd & 0xF) == 0) doRetrg(ch);
    }
}

void Replayer::volumeFineUp(int ch) {
    if (tickCounter == 0) {
        channels[ch].volume += channels[ch].cmd & 0xF;
        if (channels[ch].volume > 64) channels[ch].volume = 64;
    }
}

void Replayer::volumeFineDown(int ch) {
    if (tickCounter == 0) {
        channels[ch].volume -= channels[ch].cmd & 0xF;
        if (channels[ch].volume < 0) channels[ch].volume = 0;
    }
}

void Replayer::noteCut(int ch) {
    if (tickCounter == (channels[ch].cmd & 0xF))
        channels[ch].volume = 0;
}

void Replayer::noteDelay(int ch) {
    auto& c = channels[ch];
    if (tickCounter == (c.cmd & 0xF) && (c.note & 0xFFF) > 0)
        doRetrg(ch);
}

void Replayer::patternDelay(int ch) {
    if (tickCounter == 0 && pattDelTime2 == 0)
        pattDelTime = (channels[ch].cmd & 0xF) + 1;
}

void Replayer::updateFunk(int ch) {
    auto& c = channels[ch];
    int8_t funkSpeed = c.glissFunk >> 4;
    if (funkSpeed == 0) return;

    c.funkOffset += funkTable[funkSpeed];
    if (c.funkOffset >= 128) {
        c.funkOffset = 0;
        if (c.loopStart && c.waveStart) {
            if (++c.waveStart >= c.loopStart + (c.replen << 1))
                c.waveStart = c.loopStart;
            const_cast<int8_t*>(c.waveStart)[0] = -1 - c.waveStart[0];
        }
    }
}

} // namespace mod
