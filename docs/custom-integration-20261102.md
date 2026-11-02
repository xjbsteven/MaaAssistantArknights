# Personal Core integration on upstream v6.21.1 (2026-11-02)

This branch is **not** a released or gameplay-certified version. It is a reviewable migration based on upstream **v6.21.1**, commit `d8a98c72a45618f24e9c55fa014809d5ade6bf52`. Previous custom source: `codex/recycle-integration-20261015` at `781f10271c5bd12cc5d29acf50f2ba38dbeff57c`. Neither old branch nor the fork default branch was overwritten.

## Decision matrix

| Behavior | Prior implementation | Upstream v6.21.1 | Decision | Verification |
| --- | --- | --- | --- | --- |
| Facility-point station preset shift (including planning/layout, training, reception, recycle and drones) | `InfrastPresetTask.*`, `InfrastReceptionPresetTask.*`, `InfrastTask.*`, two PNG templates, `tasks.json` | No equivalent `rotation_style=station_preset` interface | Preserve personal implementation; merge into official `InfrastTask` and task resources | Core compilation; user gameplay for UI matching and facility mapping |
| Recycle station shift | Existing `InfrastRecycleTask` and custom preset plan | Implemented upstream | Reuse official facility task, do not override new implementation | Config/test, actual recycle room swap |
| Recycle product notification/dialog | Previously custom click fallback was proposed but not reliable | Official `InfrastRewardRecycleProductTaskPlugin` and `InfrastRewardRecycleProduct` matching, `specificRect=[640,645,1,1]` | Reuse official recognition and popup dismissal, **do not add** unsafe `(1050,450)` fixed click | Game reward popup screenshot/functional verification pending |
| Independent all-product collection | No independent option in old source | Official automatic entry notification collection | New `collect_only` param schedules only `InfrastBegin`; no facility shifts, drones or replenishment | Core compilation, contract test, game run pending |
| Dorm trust autofill, exhausted-pool detection and non-destructive selection | `DormScanLogic.h`, `InfrastDormTask.*` | Partially improved upstream (including 12 Fiammetta candidates) | Merge personal controls while retaining new upstream candidate set | C++ smoke and user gameplay pending |
| Minimum recruit guarantee | `RecruitMinimumGuarantee.h`, `RecruitTask.cpp`, `AutoRecruitTask.cpp` | Not implemented | Restore parameter and helper; preserve recruit reserve behavior | C++ smoke and Kotlin protocol tests pending |
| Conditional automatic UI-theme restore | `Status.h`, `ProcessTask.cpp`, `SwitchThemeTask.*` | Not implemented | Reintroduce `only_if_fallback` and fallback signal in official process flow | Build and app integration pending |
| New home-page recognition (PR #18955) | Legacy resources | Included in upstream stable | Use official resources | Game regression pending |
| Roguelike new-home entry (PR #19003) | Old single UI template | Upstream patch proposed, not merged when migrated | Backport one-line two-template change in `resource/tasks/Roguelike/base.json` | Game regression pending |

Five localized `base-scheduling-schema.md` files retain custom schema additions. The five `protocol/integration.md` files were extensively rewritten upstream and have not been blindly replaced; this addendum documents personal extension semantics. Two historical Catch2 test sources were retained in `unit_test/MaaCore`; upstream v6.21.1 removed the old `unit_test` build scaffolding, so these historical tests are **not** being presented as executed. A portable C++ smoke test is in `tests/custom/CustomSchedulingLogicSmoke.cpp`, intended to run in downstream Android CI.

## Android binding

Partner branch: `xjbsteven/MAA-Meow:integration/official-20261102` based on official `v0.25.1`. Its branch CI checks out this Core branch, builds Android arm64 from source, overlays **both** matching `libMaaCore.so` and `libMaaUtils.so` and `install/resource`, writes `.maaversion` with the custom Core commit SHA, and verifies the binary-embedded version and required task entries. Do **not** substitute the old hard-pinned `v6.17.3-alpha.1-custom.ee8a1ad24f` binary or verification scripts.

## Outstanding acceptance criteria

Compiler success, Kotlin unit tests, Android lint, APK build and APK packaging identity must be evaluated against GitHub Actions results for the **latest** Android commit. Nothing here claims completed validation. In-game home/roguelike/credit/public-recruit/infra/recycle/dorm/user-profile migration need explicit manual device regression. If any task misbehaves, collect Maa logs and device screenshots without overwriting original settings.
