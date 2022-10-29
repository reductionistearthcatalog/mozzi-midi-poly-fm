/*
// This is a MIDI-controlled polyphonic FM oscillator firmware for the AE Modular Grains (in Mozzi mode). It listens to MIDI channel 4, and 
// requires something like Hairless MIDI serial bridge or ttymidi to translate between MIDI from a computer to USB serial.
// It can do 3 notes at a time, but the audio starts glitching a bit with 3 notes (in a manner keeping with the AE modular aesthetic, I think). 
// It is modified from:
// Simple DIY Electronic Music Projects
//    diyelectromusic.wordpress.com
//
//  Trinket USB-MIDI Multi Pot Mozzi FM Synthesis - Part 3
//  https://diyelectromusic.wordpress.com/2021/03/28/trinket-usb-midi-multi-pot-mozzi-synthesis-part-3/
//
      MIT License
      
      Copyright (c) 2022 the Reductionist Earth Catalog
      
      Permission is hereby granted, free of charge, to any person obtaining a copy of
      this software and associated documentation files (the "Software"), to deal in
      the Software without restriction, including without limitation the rights to
      use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
      the Software, and to permit persons to whom the Software is furnished to do so,
      subject to the following conditions:
      
      The above copyright notice and this permission notice shall be included in all
      copies or substantial portions of the Software.
      
      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
      FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
      COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHERIN
      AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
      WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*
  Using principles from the following Arduino tutorials:
    Arduino MIDI Library  - https://github.com/FortySevenEffects/arduino_midi_library
    Mozzi Library         - https://sensorium.github.io/Mozzi/
    Arduino Potentiometer - https://www.arduino.cc/en/Tutorial/Potentiometer
  Much of this code is based on the Mozzi example Knob_LightLevel_x2_FMsynth (C) Tim Barrass
*/
#include <MozziGuts.h>
#include <Oscil.h> // oscillator
#include <tables/cos2048_int8.h> // for the modulation oscillators
#include <tables/sin2048_int8.h> // sine table for oscillator
#include <tables/saw2048_int8.h> // saw table for oscillator
#include <tables/triangle2048_int8.h> // triangle table for oscillator
#include <tables/square_no_alias_2048_int8.h> // square table for oscillator
#include <mozzi_midi.h>
#include <Smooth.h>
#include <ADSR.h>

#include <MIDI.h>
struct CustomBaudRate : public midi::DefaultSettings{
  static const long BaudRate = 115200;
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, CustomBaudRate);

#define ALGREAD mozziAnalogRead
#define POT_ZERO 15 // Anything below this value is treated as "zero"
// Set up the analog inputs - comment out if you aren't using this one
#define MODR_PIN A1  // Modulation Ratio
#define INTS_PIN A2  // FM intensity
//#define INTS_CV A3 // FM intensity CV
#define CV_GATE_OUT 8 // added for gate out

// Default potentiometer values if no pot defined
#define DEF_potWAVT 2
#define DEF_potMODR 5
#define DEF_potINTS 500

#define CONTROL_RATE 64   // Hz, powers of 2 are most reliable

#define NUM_OSC 3 

Oscil<COS2048_NUM_CELLS, AUDIO_RATE> aCarrier[NUM_OSC];
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> aModulator[NUM_OSC];

int mod_ratio;
int carrier_freq[NUM_OSC];
long fm_intensity;
int playing[NUM_OSC];
int playidx;

// Comment this out to ignore new notes in polyphonic modes
#define POLY_OVERWRITE 1

// smoothing for intensity to remove clicks on transitions
float smoothness = 0.95f;
Smooth <long> aSmoothIntensity(smoothness);

int potcount;
int potMODR, potINTS;

// "envelope" generator (just a 1 or 0 indicating if the note is on or off)
bool envelope[NUM_OSC];

//these did go right before scanMidi, I'm trying them out here to see if it solves scoping issues
void setFreqs(int osc){
  //calculate the modulation frequency to stay in ratio
  int mod_freq = carrier_freq[osc] * mod_ratio;

  // set the FM oscillator frequencies
  aCarrier[osc].setFreq(carrier_freq[osc]);
  aModulator[osc].setFreq(mod_freq);
}

void noteOn (byte note, int osc) {
  carrier_freq[osc] = mtof(note);
  setFreqs(osc);
  envelope[osc] = true;
}

void noteOff (int osc) {
  envelope[osc] = false;
}

void HandleNoteOn(byte channel, byte note, byte velocity) {
   if (velocity == 0) {
      HandleNoteOff(channel, note, velocity);
      return;
  }
  digitalWrite(CV_GATE_OUT,HIGH); // at least one note is playing    

#ifdef POLY_OVERWRITE
  // Overwrites the oldest note playing
  playidx++;
  if (playidx >= NUM_OSC) playidx=0;
  playing[playidx] = note;
  noteOff(playidx);
  noteOn(note, playidx);
#else
  // Ignore new notes until there is space
  for (int i=0; i<NUM_OSC; i++) {
    if (playing[i] == 0) {
      playing[i] = note;
      noteOn(note, i);
      return;
    }
  }
#endif
}

void HandleNoteOff(byte channel, byte note, byte velocity) {
  for (int i=0; i<NUM_OSC; i++) {
    if (playing[i] == note) {
      // If we are still playing the same note, turn it off
      noteOff(i);
      playing[i] = 0;
    }
    digitalWrite(CV_GATE_OUT,LOW); //try ending gate if any notes off received
  }
}

// control change. this is for a MIDI handshake so that software like supercollider can tell the difference between multiple GRAINS modules connected by USB to host.
void HandleControlChange(byte channel, byte num, byte val)
{
    if (num == 3) {
      MIDI.sendControlChange(127,3,1); // if receive cc 3, respond with handshake cc. In this case, cc#127, value 3 for this firmware, channel 1
    }
}

void setup(){
  pinMode(CV_GATE_OUT, OUTPUT);

  // Connect the HandleNoteOn function to the library, so it is called upon reception of a NoteOn.
  MIDI.setHandleNoteOn(HandleNoteOn);  // Put only the name of the function
  MIDI.setHandleNoteOff(HandleNoteOff);  // Put only the name of the function
  MIDI.setHandleControlChange(HandleControlChange);
  // Initiate MIDI communications, listen to channel 4
  MIDI.begin(4);
  Serial.begin(115200);

  for (int i=0; i<NUM_OSC; i++) {
      aCarrier[i].setTable(SIN2048_DATA);
  }

  for (int i=0; i<NUM_OSC; i++) {
    aModulator[i].setTable(COS2048_DATA);
  }

  // Set default parameters for any potentially unused/unread pots
  potcount = 0;

  startMozzi(CONTROL_RATE);
}

void updateControl(){
  MIDI.read();
  // Read the potentiometers.  This is one of the most time-intensive tasks
  // so only read one at a time.  We can also use this for other tasks that
  // don't need to be performed on every control scan.
  //
  // Note: each potXXXX value is remembered between scans.
  potcount ++;
  switch (potcount) {
  case 1:
#ifdef INTS_PIN
    potINTS = ALGREAD(INTS_PIN); // value is 0-1023
    if (potINTS<POT_ZERO) potINTS = 0;
#else
    potINTS = DEF_potINTS
#endif
    break;
  case 2:
#ifdef MODR_PIN
    potMODR = ALGREAD(MODR_PIN) >> 7; // value is 0-7
#else
    potMODR = DEF_potMODR;
#endif
    break;
 default:
    potcount = 0;
 }

  // Everything else from this point on has to be updated at the
  // CONTROL_RATE - oscillators, envelopes, etc.

  mod_ratio = potMODR;

  // calculate the fm_intensity
  fm_intensity = (long)potINTS;

  for (int i=0; i<NUM_OSC; i++) {
    setFreqs(i);
  }
}


int updateAudio(){
  // now trying the approach from here: https://github.com/jidagraphy/mozzi-poly-synth/blob/master/mozzi-poly-synth.ino
  int output=0;
  long fmmod = aSmoothIntensity.next(fm_intensity);
  for (int i=0; i<NUM_OSC; i++) {
    if (envelope[i]) {
      long modulation = fmmod * aModulator[i].next();
      output += (int) ((aCarrier[i].phMod(modulation))>>3);
    }
  }
  return (output);
}

void loop(){
  audioHook();
}
