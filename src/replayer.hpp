#pragma once

#include <memory>
#include "types.hpp"
#include "renderer.hpp"

namespace mod {

class Replayer {
public:
    Replayer();

    void setModule(Module* mod);
    void play();
    void stop();
    bool isPlaying() const { return playing; }

    void tick();
    double ciaBpm2Hz(int bpm) const;

    void setRenderer(IRenderer* r) { renderer = r; }

    int getBPM() const { return bpm; }
    int getSpeed() const { return speed; }
    int getRow() const { return row; }
    int getPosition() const { return position; }
    int getPattern() const { return pattern; }

private:
    void playVoice(int ch);
    void checkEffects(int ch);
    void checkMoreEffects(int ch);
    void setDMA();

    void setPeriod(int ch);
    void setTonePorta(int ch);
    void tonePortNoChange(int ch);
    void tonePortamento(int ch);
    void vibrato(int ch);
    void vibrato2(int ch);
    void tremolo(int ch);
    void volumeSlide(int ch);
    void arpeggio(int ch);
    void portaUp(int ch);
    void portaDown(int ch);
    void sampleOffset(int ch);
    void volumeChange(int ch);
    void positionJump(int ch);
    void patternBreak(int ch);
    void setSpeed(int ch);
    void eCommands(int ch);
    void finePortaUp(int ch);
    void finePortaDown(int ch);
    void setGlissControl(int ch);
    void setVibratoControl(int ch);
    void setFineTune(int ch);
    void jumpLoop(int ch);
    void setTremoloControl(int ch);
    void retrigNote(int ch);
    void volumeFineUp(int ch);
    void volumeFineDown(int ch);
    void noteCut(int ch);
    void noteDelay(int ch);
    void patternDelay(int ch);
    void doRetrg(int ch);
    void updateFunk(int ch);

    Module* module = nullptr;
    IRenderer* renderer = nullptr;
    std::array<Channel, NUM_CHANNELS> channels;

    bool playing = false;
    int tickCounter = 0;
    int speed = 6;
    int bpm = 125;
    int row = 0;
    int position = 0;
    int pattern = 0;

    bool posJumpAssert = false;
    bool pBreakFlag = false;
    int8_t pBreakPosition = 0;
    uint8_t pattDelTime = 0;
    uint8_t pattDelTime2 = 0;
    uint8_t lowMask = 0xFF;
    uint16_t dmaCon = 0;
    int ciaSetBPM = -1;
};

extern const int16_t periodTable[];
extern const uint8_t vibratoTable[];
extern const uint8_t funkTable[];

} // namespace mod
