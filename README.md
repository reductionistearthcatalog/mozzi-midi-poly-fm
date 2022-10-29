# mozzi-midi-poly-fm
MIDI-controlled polyphonic FM oscillator firmware for the AE Modular Grains (in Mozzi mode).

It listens to MIDI channel 4, and requires something like Hairless MIDI serial bridge or ttymidi to translate between MIDI from a computer to USB serial. It can do 3 notes at a time, but the audio starts glitching a bit with 3 notes (in a manner keeping with the AE modular aesthetic, I think). It is modified from:
 Simple DIY Electronic Music Projects
    diyelectromusic.wordpress.com
  Trinket USB-MIDI Multi Pot Mozzi FM Synthesis - Part 3
  https://diyelectromusic.wordpress.com/2021/03/28/trinket-usb-midi-multi-pot-mozzi-synthesis-part-3/
