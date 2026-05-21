-- Intro cutscene. Runs as a coroutine: each show_text yields until the page's
-- duration elapses (or the player skips); wait() pauses between pages.
show_text("El Cairo, 1936.")
show_text("La Dra. Julia llega al museo al anochecer.")
wait(0.4)
show_text("Las puertas estan abiertas. Algo no anda bien...")
