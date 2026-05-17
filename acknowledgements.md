# Acknowledgements

This project stands on a lot of prior open source work, both from the
Twilight Princess randomizer / modding community and the broader C++
ecosystem.

## Bundled libraries
- [SDL3](https://github.com/libsdl-org/SDL) — Zlib
- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [nlohmann/json](https://github.com/nlohmann/json) — MIT
- [dolphin-memory-engine](https://github.com/aldelaro5/dolphin-memory-engine) — MIT
- [Inter font](https://github.com/rsms/inter) — SIL Open Font License 1.1

## Game data and logic
- [Twilight Princess Randomizer — web generator](https://github.com/zsrtp/Randomizer-Web-Generator) (MIT) — randomizer logic engine, predicates, and the room/check graph bundled in `data/world/`.
- [Twilight Princess apworld](https://github.com/WritingHusky/Twilight_Princess_apworld) (MIT) — `data/locations.json` is derived from its location table.

Memory offsets and layouts for a lot of the data this project uses were
not reverse-engineered from scratch, and were instead collected from
previous open source work and then verified by testing and memory
probing in a running game. The main upstream sources used this way are
the [TPR offline patcher](https://github.com/zsrtp/Randomizer),
[libtp_rel](https://github.com/zsrtp/libtp_rel), and the
[Twilight Princess apworld](https://github.com/WritingHusky/Twilight_Princess_apworld)
above.

## Check explanations

Text descriptions in `explanations/` were mostly sourced from the
[Twilight Princess Randomizer Wiki — List of Randomizer Checks](https://wiki.tprandomizer.com/index.php?title=List_of_Randomizer_Checks),
but some were sourced from other community websites:

- [Zelda Dungeon — Twilight Princess walkthrough](https://www.zeldadungeon.net/twilight-princess-walkthrough/)
- [Zelda Universe — Hidden Skills guide](https://zeldauniverse.net/guides/twilight-princess/sidequests/hidden-skills/)
- [Zelda Universe — Poe Souls guide](https://zeldauniverse.net/guides/twilight-princess/sidequests/poe-souls/)
- [Zelda Fandom — Coro's Store](https://zelda.fandom.com/wiki/Coro%27s_Store)
- [Zelda Fandom — Ancient Sky Book](https://zelda.fandom.com/wiki/Ancient_Sky_Book)
- [Zelda Fandom — Owl Statue](https://zelda.fandom.com/wiki/Owl_Statue)
- [StrategyWiki — Twilight Princess](https://strategywiki.org/wiki/The_Legend_of_Zelda:_Twilight_Princess)

Per-check source attribution is in
[`explanations/REFERENCES.md`](explanations/REFERENCES.md).
