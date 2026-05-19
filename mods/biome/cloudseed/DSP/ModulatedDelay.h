/*
Copyright (c) 2024 Ghost Note Engineering Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#include "ModulatedDelay.h"
#include "Utils.h"
#include <stdint.h>
#include <cstdlib> // ER-301 port: RAND_MAX / rand()
#include <cmath>   // ER-301 port: fmod()

namespace Cloudseed
{
	class ModulatedDelay
	{
	private:

		static const int ModulationUpdateRate = 8;
		static const int DelayBufferSize = 50000; // ER-301 port: ~1s+ at 48kHz (was 384000 = 4s at 192kHz). Max LateLineSize is 1000ms.

		float delayBuffer[DelayBufferSize] = { 0 };
		int writeIndex;
		int readIndexA;
		int readIndexB;
		uint64_t samplesProcessed;

		float modPhase;
		float gainA;
		float gainB;

	public:
		int SampleDelay;

		float ModAmount;
		float ModRate;

		ModulatedDelay()
		{
			writeIndex = 0;
			readIndexA = 0;
			readIndexB = 0;
			samplesProcessed = 0;

			modPhase = 0.01 + 0.98 * (rand() / (float)RAND_MAX); // ER-301 port: drop std::
			gainA = 0;
			gainB = 0;

			SampleDelay = 100;
			ModAmount = 0.0;
			ModRate = 0.0;

			Update();
		}

		void Process(float* input, float* output, int bufSize)
		{
			for (int i = 0; i < bufSize; i++)
			{
				if (samplesProcessed >= ModulationUpdateRate)
				{
					Update();
					samplesProcessed = 0;
				}

				delayBuffer[writeIndex] = input[i];
				output[i] = delayBuffer[readIndexA] * gainA + delayBuffer[readIndexB] * gainB;

				writeIndex++;
				readIndexA++;
				readIndexB++;
				if (writeIndex >= DelayBufferSize) writeIndex -= DelayBufferSize;
				if (readIndexA >= DelayBufferSize) readIndexA -= DelayBufferSize;
				if (readIndexB >= DelayBufferSize) readIndexB -= DelayBufferSize;
				samplesProcessed++;
			}
		}

		void ClearBuffers()
		{
			Utils::ZeroBuffer(delayBuffer, DelayBufferSize);
		}


	private:
		void Update()
		{
			modPhase += ModRate * ModulationUpdateRate;
			if (modPhase > 1)
				modPhase = fmod(modPhase, 1.0); // ER-301 port: drop std::

			auto mod = sinf(modPhase * 2 * M_PI); // ER-301 port: drop std::
			auto totalDelay = SampleDelay + ModAmount * mod;

			auto delayA = (int)totalDelay;
			auto delayB = (int)totalDelay + 1;

			auto partial = totalDelay - delayA;

			gainA = 1 - partial;
			gainB = partial;

			readIndexA = writeIndex - delayA;
			readIndexB = writeIndex - delayB;
			if (readIndexA < 0) readIndexA += DelayBufferSize;
			if (readIndexB < 0) readIndexB += DelayBufferSize;
		}
	};
}
