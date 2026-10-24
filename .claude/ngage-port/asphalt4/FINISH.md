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

## Result

(To be filled in after the run.)
