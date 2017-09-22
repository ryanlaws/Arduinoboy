/**************************************************************************
 * Name:    Timothy Lamb                                                  *
 * Email:   trash80@gmail.com                                             *
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

void modeLSDJMidioutSetup()
{
  digitalWrite(pinStatusLed,LOW);
  pinMode(pinGBClock,OUTPUT);
  digitalWrite(pinGBClock,HIGH);

 #ifdef MIDI_INTERFACE
  usbMIDI.setHandleRealTimeSystem(NULL);
 #endif

  countGbClockTicks=0;
  lastMidiData[0] = -1;
  lastMidiData[1] = -1;
  midiValueMode = false;
  blinkMaxCount=60;
  modeLSDJMidiout();
}

void modeLSDJMidiout()
{
  while(1){
     if(getIncommingSlaveByte()) {
        if(incomingMidiByte > 0x6f) {
          switch(incomingMidiByte)
          {
           case 0x7F: //clock tick
             serial->write(0xF8);
#ifdef MIDI_INTERFACE
             usbMIDI.sendRealTime((int)0xF8);
#endif
             break;
           case 0x7E: //seq stop
             serial->write(0xFC);
#ifdef MIDI_INTERFACE
             usbMIDI.sendRealTime((int)0xFC);
#endif
             stopAllNotes();
             break;
           case 0x7D: //seq start
             serial->write(0xFA);
#ifdef MIDI_INTERFACE
             usbMIDI.sendRealTime((int)0xFA);
#endif
             break;
           default:
             midiData[0] = (incomingMidiByte - 0x70);
             midiValueMode = true;
             break;
          }
        } else if (midiValueMode == true) {
          midiValueMode = false;
          midioutDoAction(midiData[0],incomingMidiByte);
        }

      } else {
        setMode();                // Check if mode button was depressed
        updateBlinkLights();
#ifdef MIDI_INTERFACE
        while(usbMIDI.read()) ;
#endif
        if (serial->available()) {                  //If serial data was send to midi inp
          checkForProgrammerSysex(serial->read());
        }
      }
   }
}

void midioutDoAction(byte m, byte v)
{
  if(m < 4) {
    //note message
    if(v) {
      checkStopNote(m);
      playNote(m,v);
    } else if (midiOutLastNote[m]>=0) {
      stopNote(m);
    }
  } else if (m < 8) {
    m-=4;
    //cc message
    playCC(m,v);
  } else if(m < 0x0C) {
    m-=8;
    playPC(m,v);
  }
  blinkLight(0x90+m,v);
}

void checkStopNote(byte m)
{
  if((midioutNoteTimer[m]+midioutNoteTimerThreshold) < millis()) {
    stopNote(m);
  }
}

void stopNote(byte m)
{
  for(int x=0;x<midioutNoteHoldCounter[m];x++) {
    midiData[0] = (0x80 + (getChannel(m)));
    midiData[1] = midioutNoteHold[m][x];
    midiData[2] = 0x00;
    serial->write(midiData,3);
#ifdef MIDI_INTERFACE
    usbMIDI.sendNoteOff(midioutNoteHold[m][x], 0, getChannel(m)+1);
#endif
  }
  midiOutLastNote[m] = -1;
  midioutNoteHoldCounter[m] = 0;
}

void playNote(byte m, byte n)
{
  if (m == 3) {
    setVolcaSampleChannel(n);
  }
  midiData[0] = (0x90 + (getChannel(m)));
  midiData[1] = n;
  midiData[2] = 0x7F;
  serial->write(midiData,3);
#ifdef MIDI_INTERFACE
  usbMIDI.sendNoteOn(n, 127, getChannel(m)+1);
#endif

  midioutNoteHold[m][midioutNoteHoldCounter[m]] =n;
  midioutNoteHoldCounter[m]++;
  midioutNoteTimer[m] = millis();
  midiOutLastNote[m] =n;
}

void playCC(byte m, byte n)
{
  byte v = n;

  if(memory[MEM_MIDIOUT_CC_MODE+m]) {
    if(memory[MEM_MIDIOUT_CC_SCALING+m]) {
      v = (v & 0x0F)*8;
    }
    n=(m*7)+((n>>4) & 0x07);
  } else {
    if(memory[MEM_MIDIOUT_CC_SCALING+m]) {
      float s;
      s = n;
      v = ((s / 0x6f) * 0x7f);
    }
    n=(m*7);
  }
  midiData[0] = (0xB0 + (memory[MEM_MIDIOUT_CC_CH+m]));
  midiData[1] = (memory[MEM_MIDIOUT_CC_NUMBERS+n]);
  midiData[2] = v;
  serial->write(midiData,3);
#ifdef MIDI_INTERFACE
  usbMIDI.sendControlChange((memory[MEM_MIDIOUT_CC_NUMBERS+n]), v, getChannel(m)+1);
#endif
}

void playPC(byte m, byte n)
{
  midiData[0] = (0xC0 + (getChannel(m)));
  midiData[1] = n;
  serial->write(midiData,2);
#ifdef MIDI_INTERFACE
  usbMIDI.sendProgramChange(n, getChannel(m)+1);
#endif
}

void stopAllNotes()
{
  for(int m=0;m<4;m++) {
    if(midiOutLastNote[m]>=0) {
      stopNote(m);
    }
    midiData[0] = (0xB0 + (getChannel(m)));
    midiData[1] = 123;
    midiData[2] = 0x7F;
    serial->write(midiData,3); //Send midi
#ifdef MIDI_INTERFACE
    usbMIDI.sendControlChange(123, 127, getChannel(m)+1);
#endif
  }
}

boolean getIncommingSlaveByte()
{
  delayMicroseconds(midioutBitDelay);
  GB_SET(0,0,0);
  delayMicroseconds(midioutBitDelay);
  GB_SET(1,0,0);
  delayMicroseconds(2);
  if(digitalRead(pinGBSerialIn)) {
    incomingMidiByte = 0;
    for(countClockPause=0;countClockPause!=7;countClockPause++) {
      GB_SET(0,0,0);
      delayMicroseconds(2);
      GB_SET(1,0,0);
      incomingMidiByte = (incomingMidiByte << 1) + digitalRead(pinGBSerialIn);
    }
    return true;
  }
  return false;
}

byte getChannel(byte m)
{
  return m < 0x3 ? memory[MEM_MIDIOUT_NOTE_CH+m] : volcaSampleChannel;
}

void setVolcaSampleChannel(byte n)
{
  volcaSampleChannel = n % 10;
}
