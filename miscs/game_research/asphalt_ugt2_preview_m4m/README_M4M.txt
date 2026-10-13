ASPHALT URBAN GT 2 - PREVIEW 2.0.157 - M4M "CHEATS" TEST BUILD
==============================================================
Status: statically verified + CPU-emulated call path only. NOT yet run in
EKA2L1 or on hardware. Please report what you see.

WHAT IT DOES
  Restores the original CHEATS entry in the in-race Pause menu.
  Built on M4B, so the red FPS and white AI "ERR" overlays are still there.

INSTALL (EKA2L1 Android)
  1. Back up your current  E:\SYSTEM\APPS\6RBC\6RBC.APP  and your save data.
  2. Copy SYSTEM\APPS\6RBC\6RBC.APP from this ZIP over it.
     Do not change 6RBC.DAT, GAME.LIC or any other file.

TEST
  1. Start a SINGLE-PLAYER race. Press the RIGHT soft key to open Pause.
     Expected: JUKEBOX, SOUND, RESUME RACE, RESTART RACE, QUIT RACE,
               CHEATS, EXIT GAME
     CHEATS is hidden on purpose in multiplayer / Bluetooth sessions.
  2. Move to CHEATS. A "< ... >" selector should appear at the bottom of the
     screen, in the same spot the JUKEBOX track selector uses.
  3. Use LEFT/RIGHT to scroll through the cheats. Confirm with the CENTRE/FIRE
     key ("F" on the EKA2L1 on-screen pad). The left soft key already crashes
     the unpatched game, so avoid it.
  4. Try a harmless toggle first ("Show FPS", "Hide HUD", "Camera Front").
     Try "Win Race", "Gimme All Cars!", "GIVE $10,000" and the UNLOCK entries
     only after you have backed up your saves.
  Entries found in the code: UNLOCK CHAMPIONSHIPS, UNLOCK CHALLENGES,
  UNLOCK ARCADE CATEGORIES, Win All Arcade Categories, Win All Championships,
  FREEZE VEHICLES, SHOW VEHICLE COORDINATES, Show FPS, Camera Front, Hide HUD,
  Show MP Lantecy, Show AI Errors, GIVE $10,000, Gimme All Cars!,
  Raise Cop Level, Win Race.

PLEASE REPORT
  - whether CHEATS appears, and whether all 7 rows fit inside the box
  - whether the bottom selector appears and LEFT/RIGHT changes it
  - what each cheat you try actually does, or whether it crashes

ROLLBACK
  Copy back the file you saved, or use ROLLBACK_M4B / ROLLBACK_ORIGINAL from
  the "rollback_and_notes" ZIP.

SHA-256
  M4M 6RBC.APP (this ZIP)  7c8f2b7069ef7a6bdac0616fc9609644323c5810a0fa7f0978ad51a8785deafc
  M4B (base, rollback)     4fecdaf5d74c40cb303c855777dec0331e55570c8a2637b2d2f5c65454903b6d
  Original preview         aa9c88194d1012aa3e0c50dc84cc6e5b87e472112f009cb620ad1603312b53b3
