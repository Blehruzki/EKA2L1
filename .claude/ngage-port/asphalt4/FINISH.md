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

## Update (round A6): the autopilot now finishes the race; no per-frame spike seen

`tools/autodrive.py` was rewritten and now **completes a full three-lap Instant
Play race** and crosses the finish. What it took:
- **Fast capture.** An Xlib sub-rectangle grab (~5 ms) instead of spawning a
  capture tool, so the loop runs ~15 Hz and can react at racing speed.
- **Continuous proportional steering** holding the d-pad toward the road centroid.
- **The key fix: recovery must not over-rotate.** A long corrective turn at a
  junction spins the car until it faces back down the track; the game then shows
  "WRONG WAY / REVERSE" and stops counting, so the lap never completes. That is
  why the round-A5 versions never cleared a lap. Short, gentle recovery clears
  junctions without turning the car around. It now does laps 1, 2 and 3.

**The finish was reached and the race-end cost captured** (dyncom, FRAMESTAT,
`meas.log`): across the racing-to-garage transition the per-frame cost is
**mean 7.95 M, peak 10.26 M** -- the same regime as steady racing (~8-9.5 M),
dropping to ~6 M on the static tuning/garage menu afterward. **No per-frame
instruction spike at the race end.**

**But the finish/results *animation* itself is not cleanly isolated.** The
autopilot holds accelerate, which also confirms menus, so it rushes through the
results screen into the garage before it can be measured; an attempt to make it
idle at the finish mis-fired mid-race (a no-road stretch at a wall/tunnel looks
the same as the results screen to the detector; the speedometer's presence is
the better discriminator and is not yet wired in). So what is shown is that the
race-end *transition frames* cost no more than racing -- not a clean reading of
a slow-motion finish cinematic, if one plays.

**What this says about the user's report.** On the bench the CPU cost per frame
does **not** jump when the race ends. So the "much worse at the finish" seen on
the phone is most likely **not** a per-frame CPU/renderer spike. More likely
candidates, none yet measured: the results screen rendering a detailed rotating
3D car (heavier individual frames, fewer of them), a load/stream hitch as the
race tears down, or audio. Confirming which needs a clean finish capture.

**To nail the finish animation**, the cheapest routes:
1. Capture it on the phone (the user reaches it in normal play) -- a photo, or
   a count of how long the slowdown lasts, pins what it is.
2. Wire the speedometer-presence check into the autopilot so it idles exactly
   at the race end and the results sequence plays untouched under FRAMESTAT.
3. A save state at a race finish, loaded under the profiler.

The honest status: the autopilot finishes the race, and the race-end transition
costs no more per frame than racing; a distinct finish-animation slowdown was
reported on the phone but has not been reproduced or isolated on the bench.

## Clean capture (round A7): the finish costs no more per frame than racing

The autopilot was given a reliable race-end detector -- a tight box on the
**speedometer dial face** (bright ~0.8 whether the scene behind is a lit street
or a dark tunnel; gone only on the results screen), counted as the finish only
after it stays gone ~14 s (a cinematic such as TAKEDOWN hides the HUD only
briefly and then it returns). On that signal the autopilot releases every key
and idles, so the end-of-race screen plays untouched under FRAMESTAT instead of
being skipped by held accelerate.

It worked. The autopilot drove a full three-lap Instant Play race (came 8th),
and at the finish the game showed **"RACE LOST -- DO YOU WANT TO RETRY? YES/NO"**
-- a dialog over the dimmed, frozen race scene (`a4/F-real-*.png`, frame 3776+).
That is the end-of-race state, captured cleanly.

**Per-frame cost (same dyncom run, FRAMESTAT, `dmeas.log`):**

| | mean | peak/max |
|---|---|---|
| steady racing (frames 3000-3760) | 5.81 M | 10.56 M |
| **finish screen** (RACE LOST dialog, 3790-4030) | **5.20 M** | **7.58 M** |

The transition is smooth, no spike (per-frame 3760-3820 stays 4-7 M throughout).
**The finish screen costs *less* per frame than racing, not more** -- it is a 2D
dialog over a dimmed, frozen 3D scene, so there is less to draw than active
racing with traffic and effects.

**So the phone's "much worse at the finish" is not a per-frame CPU/renderer
spike.** On the bench the race end is cheaper per frame, not dearer. Whatever
the slowdown is on hardware, the per-frame instruction cost does not explain it.
Remaining candidates, none measured and all outside what instructions-per-frame
captures: a load/stream or allocation hitch as the race tears down and the
results screen builds, memory pressure, or audio. These would show as a stall
(a few very long frames or a pause), not as every frame being heavier -- a
wall-clock frame-time trace or the phone would catch that; the instruction
counter would not.

**One caveat on scope.** This is the *lost*-race dialog (the autopilot finished
last). A won race may play a different sequence -- a victory screen, standings,
a rotating 3D car -- that was not captured here. But for the end-of-race state
that was measured, there is no per-frame cost increase. If the user's slowdown
is specifically on a *won* race, that sequence is the one thing left to capture
(drive to a win, or reach it on the phone).

Tools used: `tools/autodrive.py` (now finishes races and idles at the end) and
the throwaway `finwatch.py` gauge watcher. No game modified; bench restored to
`dynarmic`.
