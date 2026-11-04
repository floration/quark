# Next (full)

## Latency / F3
- **Frame pacing graph** (owner, 2026-11-04: "the f3 screen should also have a frametime graph on it to check frame pacing"; "5 second history ... 50% of your frametime up and down, unless somehow you escape it then that becomes the new max/min while its in the graph"). F3 stages 3 and 4 draw it under the mouse-to-photon graph (in menus, where that graph isn't drawn, under the text lines).
  - Storage: 500 columns of 10 ms (`Game::ftCols_`), each its fastest and slowest frame plus a sum and count for the average. The 2048-frame `frameTimes_` ring can't hold 5 s at thousands of FPS (5 s at 15,000 FPS is 75,000 frames), so columns are fixed-size whatever the frame rate: one store and two compares a frame.
  - A frame longer than a column fills every column it spanned with its time (a hitch shows as a block as wide as it lasted), but counts once in the average.
  - Bounds: average ±50%; the slowest / fastest frame on the graph widens them until it scrolls off.
  - Fed only while F3 shows the graph (`perfStage_ >= 3`), emptied each time it's opened: nothing extra runs with F3 off (the pipeline marks and GPU timestamps were already gated by `perfOn()`).
  - Width: up to 40% of the screen, so it stays left of the HUD's centre bars.
- F3's GPU WAIT row was labelled (ULTRA) under Latent Sync with LOW LATENCY OFF (Latent Sync paces like ULTRA, `ultraPaced()`); it now says (LATENT SYNC) there.
