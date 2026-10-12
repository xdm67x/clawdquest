// Entry point: load saved progress, then draw.
(async () => {
  await load();
  len = S.len;
  document.querySelectorAll('[data-len]').forEach(b => b.classList.toggle('on', +b.dataset.len === len));
  render();
})();
