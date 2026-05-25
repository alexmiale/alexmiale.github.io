#include <Bela.h>
#include <vector>
#include "MonoFilePlayer.h"
#include <cmath>
#include "wavetable.h"
#include "Debouncer.h"

// Name of the sound file (in project folder)
std::string gFilename = "dry-test-loop.wav";

// Object that handles playing sound from a buffer
MonoFilePlayer gPlayer;

// Declare variables for circular buffer
std::vector<float> gCircularBuffer;
unsigned int gFlangerWritePointer = 0;
unsigned int gFlangerBufferLength = 0;

// Pin declarations
const int kFlangerButtonPin = 0; // input 
const int kFlangerLEDPin = 1; // output
const int kReverbButtonPin = 2; // input
const int kReverbLEDPin = 3; // output

// Wavetable oscillator
Wavetable gOscillator;

// Button debouncer object
Debouncer gFlangerDebouncer;
Debouncer gReverbDebouncer;

// Other flanger variables
float gFlangerFrequency = 0.3; // LFO Frequency 
float gSweepWidth = 0.01; // Range of LFO (0.5 - 15ms [0.0005 - 0.015]) 
float gFlangerDepth = 0.7; // Delayed signal mixed with normal (0 - 1)
float gFlangerFeedback = 0.3; 
bool gFlangerOn = true;

const float range = 3.3 / 4.096; // range of board

// declare variables for Reverb
std::vector<float> gDelayBuffer;
std::vector<float> gOutputBuffer;
std::vector<float> gAllpassBuffer;
unsigned int gReverbWritePointer = 0;
unsigned int gReverbReadPointer = 0;
float gDelayInSeconds = 0.034;
float gAmplitude = 0.25;
float gFeedbackCoefficient = 0.45;
float gBufferSize = 0.05; // set max buffer size in seconds
bool gReverbOn = true;

bool setup(BelaContext *context, void *userData)
{
	/*
	// Load the audio file
	if(!gPlayer.setup(gFilename)) {
    	rt_printf("Error loading audio file '%s'\n", gFilename.c_str());
    	return false;
	}

	// Print some useful info
    rt_printf("Loaded the audio file '%s' with %d frames (%.1f seconds)\n", 
    			gFilename.c_str(), gPlayer.size(),
    			gPlayer.size() / context->audioSampleRate);
    			*/
    			
	std::vector<float> wavetable;
	unsigned int wavetableSize = context->audioSampleRate * 0.2;
		
	// Populate a buffer with a sine wave
	wavetable.resize(wavetableSize);
	for(unsigned int n = 0; n < wavetable.size(); n++) 
	{
		wavetable[n] = 0.5 + (0.5 * sinf(2.0 * M_PI * (float)n / (float)wavetable.size())); // values of 0 to 1 for LFO
	}
	
	// Initialise the debouncer with 50ms interval
	gFlangerDebouncer.setup(context->audioSampleRate, .05);
	gReverbDebouncer.setup(context->audioSampleRate, .05);
	
	// Initialise the wavetable, passing the sample rate and the buffer
	gOscillator.setup(context->audioSampleRate, wavetable, true);
	gOscillator.setFrequency(gFlangerFrequency);
	
	gFlangerBufferLength = 0.3 * context->audioSampleRate; // big enough to fit the maximum delay
	gCircularBuffer.resize(gFlangerBufferLength);
	
	// Set up the digital pins
	pinMode(context, 0, kFlangerButtonPin, INPUT);
	pinMode(context, 0, kFlangerLEDPin, OUTPUT);
	
	pinMode(context, 0, kReverbButtonPin, INPUT);
	pinMode(context, 0, kReverbLEDPin, OUTPUT);
	
	// allocate the circular buffer to contain enough samples to cover 0.5 seconds
    gDelayBuffer.resize(gBufferSize * context->audioSampleRate);
    gOutputBuffer.resize(gBufferSize * context->audioSampleRate);
    gAllpassBuffer.resize(gBufferSize * context->audioSampleRate);
	
	return true;
}

// flanger function
// takes in input, frame, and context
float flanger(float in, int n, BelaContext *context) {
	float out = in;
	digitalWriteOnce(context, n, kFlangerLEDPin, HIGH);

	// inputs for full board
	float input0 = analogRead(context, n/2, 0);
	float input1 = analogRead(context, n/2, 1);
	float input2 = analogRead(context, n/2, 2);
	float input3 = analogRead(context, n/2, 3); 
	gFlangerFrequency = map(input0, 0, range, 0, 2);
	gSweepWidth = map(input1, 0, range, 0.0005, 0.015);
	gFlangerFeedback = map(input2, 0, range, 0.2, 0.99);
	gFlangerDepth = map(input3, 0, range, 0.1, 0.99);
	
	gOscillator.setFrequency(gFlangerFrequency);
	
	// get the current delay
	float currentDelay = gOscillator.process() * gSweepWidth * context->audioSampleRate;
	
	// update readpointer
	float readPointer = fmodf((float)gFlangerWritePointer - (float)currentDelay + (float)(gFlangerBufferLength - 3.0), (float)gFlangerBufferLength);
	
	// linear interpolation
	float frac = readPointer - floorf(readPointer);
	const int below = int(readPointer);
	const int above = (below + 1) % gFlangerBufferLength;
	float interpolate = ((1 - frac) * (gCircularBuffer[below])) + (frac * gCircularBuffer[above]);
			
	// store interpolated sample in cicular buffer
	gCircularBuffer[gFlangerWritePointer] += (interpolate * gFlangerFeedback);
		
	out += interpolate * gFlangerDepth;
	out *= 0.707;
	return out;
}

float reverb(float in, int n, BelaContext *context) {
	float out = in;
	digitalWriteOnce(context, n, kReverbLEDPin, HIGH);
	
	// for current board with two buttons, leds, and potentiometers
	float input0 = analogRead(context, n/2, 4);
	gFeedbackCoefficient = map(input0, 0, range, 0.3, 0.55);

	float comb;
	
	// getting old samples for IIR
    float oldIn = gDelayBuffer[gReverbReadPointer];
    float oldOut = gOutputBuffer[gReverbReadPointer];
    
    // implement Schroeder comb
    comb = oldIn + gFeedbackCoefficient*oldOut;
    // only need to use allpass buffer when reverb is on
    gAllpassBuffer[gReverbWritePointer] = comb;
    // implement Schroeder allpass
    out = (0 - gFeedbackCoefficient)*comb + gAllpassBuffer[gReverbReadPointer] + gFeedbackCoefficient*oldOut;

    return out;
}

void render(BelaContext *context, void *userData)
{
	// calculate delay
	int delayInSamples = (int)(gDelayInSeconds*context->audioSampleRate);
    
    // update read pointer
    gReverbReadPointer = (gReverbWritePointer - delayInSamples + gDelayBuffer.size()) % gDelayBuffer.size();
    
	for(unsigned int n = 0; n < context->audioFrames; n++) {
		//float in = gPlayer.process();
		float in = audioRead(context, n, 0);
		float out = in;
		
		// flanger
		// store interpolated sample in cicular buffer
		gCircularBuffer[gFlangerWritePointer] = out;
	
		// wrap write pointer 
		if (gFlangerWritePointer++ >= gFlangerBufferLength) {
			gFlangerWritePointer = 0;
		}
		
		int flangerButton = digitalRead(context, n, kFlangerButtonPin);
		gFlangerDebouncer.process(flangerButton);
		
		
		if(gFlangerDebouncer.risingEdge()) {		
			gFlangerOn = !gFlangerOn;
		}
		
		if(gFlangerOn) {
			out = flanger(out, n, context);
		} else {
			digitalWriteOnce(context, n, kFlangerLEDPin, LOW);
		}
		
		
		// reverb
		// update delay buffer 
		gDelayBuffer[gReverbWritePointer] = out;
		
		// Update the circular buffer
    	// overwrite the buffer at the write pointer
		gReverbWritePointer++;
    	if(gReverbWritePointer >= gDelayBuffer.size())
       		gReverbWritePointer = 0;

    	gReverbReadPointer++;
    	if(gReverbReadPointer >= gDelayBuffer.size())
       		gReverbReadPointer = 0;
       	
       	
		int reverbButton = digitalRead(context, n, kReverbButtonPin);
		gReverbDebouncer.process(reverbButton);
		
		if(gReverbDebouncer.risingEdge()) {	
			gReverbOn = !gReverbOn;
		}
		
		if(gReverbOn) {
			out = reverb(out, n, context);
		} else {
			digitalWriteOnce(context, n, kReverbLEDPin, LOW);
		}
		   
    	// update output buffer regardless of toggle
    	gOutputBuffer[gReverbWritePointer] = out;
    
    	// apply global amplitude
    	out*=gAmplitude;
	
		for(unsigned int channel = 0; channel < context->audioOutChannels; channel++) {
			// Write the sample to every audio output channel
    		audioWrite(context, n, channel, out);
    	}
	}
}

void cleanup(BelaContext *context, void *userData)
{

}