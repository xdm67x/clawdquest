// Entry point: load saved progress, then draw.
(async () => {
  // Must complete before rendering — otherwise early clicks would
  // overwrite saved progress with the defaults.
  await load();
  render();
})();
