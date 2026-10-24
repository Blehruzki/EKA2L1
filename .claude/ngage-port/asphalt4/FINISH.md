# Asphalt 4: the race-finish slowdown (round A5)

The user's report: Asphalt 4 plays at roughly 10-15 fps on the N95, and gets
**much worse when a race ends and the finish animation plays**. Round A4
measured the steady race at 9.50 M guest instructions a frame. This round
measures the finish, to say what it costs and why.

## Hypothesis (before measuring)

The steady race is the `Lib3D` software renderer shading each pixel once
(round A4). A finish sequence on these Gameloft titles typically adds, on top
of the still-running 3D:
- a full-screen overlay or wipe drawn every frame (a translucent flash, a
  slow-motion tint, a "FINISH"/results panel fading in), and
- more effect sprites (confetti, flares, the camera orbit showing more of the
  scene).

So the prediction is that the finish frame costs **more instructions than the
steady race frame**, with the extra landing in (a) the translucent-fill /
blend routine `+2ABAA` / `+52F44` -- the same per-pixel blend round A1 found
on the menu -- and/or (b) the span-fillers running over more of the screen.
If instead the cost is flat and the phone still drops, the slowdown is not CPU
in the renderer and the hypothesis is wrong; the numbers decide.

This is a prediction to test, not a conclusion. Round A1's rule 4 applies:
name the guess, then let the measurement move or refute it.

## Method (no game modified)

All on the capped `dyncom` bench, the unmodified Asphalt 4 exe, the same
instruments as rounds A1 and A4:

1. Drive the demo to a race, let a race **finish** on screen (the demo plays
   itself; the finish comes when its lap count runs out). Confirm by
   screenshot that the finish animation is what is on screen.
2. `EKA2L1_FRAMESTAT=10` across the transition from racing to finishing, so the
   per-frame instruction cost of the finish frames is logged next to the
   steady-race frames just before them. The ratio to the 9.50 M steady frame
   is the headline.
3. `EKA2L1_PCPROF` over a window inside the finish sequence, then
   `profreport.py` (by image) and `proffunc.py` (by function), to name where
   the extra instructions go -- in particular whether the blend `+2ABAA` /
   `+52F44` jumps from its steady-race share.
4. A screenshot of the finish frame for the record.

## What would make this answer trustworthy

- The FRAMESTAT frames said to be "the finish" must line up with a screenshot
  that shows the finish, not be inferred from timing alone (round A1: an
  instrument's coverage is a claim).
- The demo's finish must actually be reached; if the demo loops before a race
  ends, drive a real race instead and say so.
- Report the steady-race frames from the *same run* as the baseline, not
  round A4's number from another run, so bench drift cannot skew the ratio.

## Result: the finish was not reached on the bench (round A5)

**The race finish could not be measured.** It needs a race to actually end, and
on the bench that did not happen:

- **The attract demo never finishes a race.** Six minutes of the self-running
  demo (`finish-scout.log`) only cycled gameplay scenes across tracks; no
  results/finish screen, no lap completion. The two cost spikes in it (65 M and
  36 M a frame) were the first load frames, not a finish.
- **A real race has to be driven, and the bench cannot drive it.** Instant Play
  is a 3-lap race. The accelerate key is `5` (the N-Gage centre action; `Up`
  does nothing -- found by watching the speedometer). With accelerate held, the
  car needs steering it does not get: blind 5-second corrections pin it on a
  guardrail. A crude visual autopilot was built
  (`tools/autodrive.py`: grab the screen, find the grey road's centroid in a
  strip ahead of the car, steer toward it) and it does keep the car moving
  along the road -- but over a 5.5-minute run it never cleared **lap 1/3**
  (24 screenshots, all "1/3"). It is not a good enough driver to finish a lap.

So the headline question for this round -- what a finish frame costs and why --
**is unanswered.** No number for it is given, because none was measured, and the
hypothesis above is not promoted to a finding (round A1, rule 4: do not ship a
guess).

**What this round did establish** (steady race, three fresh capped-bench runs,
unmodified exe), which corroborates round A4:
- Mean steady-race cost 8.2 M instructions a frame over 579 frames, the bulk
  between 7 and 10 M, consistent with round A4's 9.5 M (the lower mean here is
  the stuck car spending time at low speed with less on screen).
- Scene cost scales with what is drawn: open road sits at 7-10 M, a dense
  urban stretch or a tunnel with traffic reaches 10-13 M -- more geometry and
  more shaded pixels cost more, as expected of the software renderer.

**To actually measure the finish**, one of:
1. Capture it on the phone (the user reaches it in normal play) -- a photo of
   the finish with the FPS counter would do, if a build exposes it.
2. A save state taken at a race finish on the bench, loaded under the profiler.
3. A better autopilot, or a scripted input that completes a lap.

The honest status: the finish slowdown is reported by the user and is plausible
from the renderer's shape, but it has not been measured here.
