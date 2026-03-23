## a sequencer scratchpad

- what is the problem?
	- the 301 has scant sequencing capability. it is actually pretty ok for gates
	- but it is bad for pitch and classic punchcard/chaselight trigger seq apps
	- algorithmic approaches are a decent way to go but how could adapt the native UI to work more attractively and provide similar put-note-here capability?
	- pages: this sucks and is not fun
	- pages but better: we get controls that are grids of steps, each represents a row or a subsequence. user can expand each control to punch in or adjust faders for step values on that row, then flit back up to top level. 36 steps, decent amount
	- what else can we do on 6 controls? select from banks, select from scales, select from individual steps
	- this last one is interesting as we could use tomf's circle graphic to implement it
	- the circle becomes a control and a readout.
	- as a control, the user can scroll between pages, each representing a step with gate toggle or pitch/mod value
	- promising, but still very annoying

## melodic CV sequencer — algorithmic approach

- outputs CV only (pitch), not gate — 301 routes these separately
- clock input advances step, output is quantized voltage
- no step-punching, no pages — musical parameters shape the melody

### core concept: constrained melodic random walk
- turing machine style memory: linearity controls repeat-vs-change
- scale quantization keeps everything musical
- parameter knobs shape the *character* of the melody, not individual notes

### candidate parameters
- **root** — key center
- **scale** — scale selection (major, minor, dorian, etc)
- **range** — how many octaves available
- **motion** — interval preference + memory. low = stepwise/repetitive, high = leapy/chaotic. combines linearity (recall vs jump) with interval weight (conjunct vs disjunct)
- **offset** — transpose within the scale, with octave traversal where implied
- **contour** — tendency of melodic direction over time: ascending, descending, arch, valley, sawtooth. not a fixed sequence but a bias on which way the next note moves
- **tension** — preference for consonant vs dissonant scale degrees. low = root/fifth heavy, high = all degrees equal or upper extensions favored

### other interesting parameters (menu or future)
- **activity** — how often pitch changes vs holds. note-change probability per clock
- **repetition** — likelihood of returning to recently played notes, creates motif-like behavior
- **phrase length** — notes before tendency to resolve toward root, creates phrase structure without explicit looping
- **register** — center point within octave range, where melody tends to sit

### visualization ideas
- something that shows the melody contour in real time
- pitch as vertical position, time scrolling horizontally
- or a circle where radius = pitch, angle = step position
- should be visually engaging and give immediate feedback on parameter changes

### constraints
- cannot couple gate and pitch (301 fundamental)
- 6 controls max visible, more in menu
- must be fun to interact with, not tedious
