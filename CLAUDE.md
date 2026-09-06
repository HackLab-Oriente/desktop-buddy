# Agentic SDLC and Spec-Driven Development

Kiro-style Spec-Driven Development on an agentic SDLC

## Project Context

### Paths
- Steering: `.kiro/steering/`
- Specs: `.kiro/specs/`

### Steering vs Specification

**Steering** (`.kiro/steering/`) - Guide AI with project-wide rules and context
**Specs** (`.kiro/specs/`) - Formalize development process for individual features

### Active Specifications
- Check `.kiro/specs/` for active specifications
- Use `/kiro-spec-status [feature-name]` to check progress

## Development Guidelines
- Think in English, generate responses in English. All Markdown content written to project files (e.g., requirements.md, design.md, tasks.md, research.md, validation reports) MUST be written in the target language configured for this specification (see spec.json.language).

## Minimal Workflow
- Phase 0 (optional): `/kiro-steering`, `/kiro-steering-custom`
- Discovery: `/kiro-discovery "idea"` — determines action path, writes brief.md + roadmap.md for multi-spec projects
- Phase 1 (Specification):
  - Single spec: `/kiro-spec-quick {feature} [--auto]` or step by step:
    - `/kiro-spec-init "description"`
    - `/kiro-spec-requirements {feature}`
    - `/kiro-validate-gap {feature}` (optional: for existing codebase)
    - `/kiro-spec-design {feature} [-y]`
    - `/kiro-validate-design {feature}` (optional: design review)
    - `/kiro-spec-tasks {feature} [-y]`
  - Multi-spec: `/kiro-spec-batch` — creates all specs from roadmap.md in parallel by dependency wave
- Phase 2 (Implementation): `/kiro-impl {feature} [tasks]`
  - Without task numbers: autonomous mode (subagent per task + independent review + final validation)
  - With task numbers: manual mode (selected tasks in main context, still reviewer-gated before completion)
  - `/kiro-validate-impl {feature}` (standalone re-validation)
- Progress check: `/kiro-spec-status {feature}` (use anytime)

## Skills Structure
Skills are located in `.claude/skills/kiro-*/SKILL.md`
- Each skill is a directory with a `SKILL.md` file
- Skills run inline with access to conversation context
- Skills may delegate parallel research to subagents for efficiency
- Additional files (templates, examples) can be added to skill directories
- `kiro-review` — task-local adversarial review protocol used by reviewer subagents
- `kiro-debug` — root-cause-first debug protocol used by debugger subagents
- `kiro-verify-completion` — fresh-evidence gate before success or completion claims
- **If there is even a 1% chance a skill applies to the current task, invoke it.** Do not skip skills because the task seems simple.

## Reviewing changes to the firmware

Most of this firmware was generated fast and proven by running it, not by
deciding it. That is a fine way to get to a working buddy and a bad way to
keep one, so changes get reviewed by agents that did not write them.

**Run the reviews before opening a PR, not after** — findings that arrive
after the PR description is written tend to get argued with instead of fixed.

### Which reviewers, and when

| agent | run it when |
|---|---|
| `review-adversarial` | **always**, for any C/C++ change |
| `review-performance` | the render path, the LED ring, anything per-frame or per-event, anything that allocates |
| `review-architecture` | a new component, a changed public interface, anything touching the bus, anything moving data between firmware and packs |
| `review-secrets` | config, NVS, the web UI, WiFi, TLS, an API key, NFC, or the Berry surface packs can call |

Documentation-only changes need none of them.

### How to run them

**Launch them in one message so they run in parallel, and give each the same
base**, which for a stacked PR is the branch below it and not `main`:

```
git diff <base>...HEAD
```

They are independent on purpose. Do not summarise one to another and do not
run them in sequence — twice now they reached the same finding by different
routes, and that agreement is the signal that it is real.

### After they report

1. **Wait for all of them before editing.** They read the same files; a fix
   applied mid-flight invalidates the report still being written.
2. **Verify before you act.** They are frequently right and occasionally
   wrong. One claimed a branch deleted three published pages; the branch was
   simply behind `main` and the diff said "deleted". Check the claim, then fix
   the code.
3. **Fix in one pass**, and say in the commit message which finding each
   change answers. A commit that says what nearly went wrong is worth more
   than one that says what was changed.
4. **Every crash-class finding becomes a host test.** `firmware/host_test/`
   under ASan and UBSan, with a CI step, and a comment naming the real defect
   the case came from. That is the bar the NDEF decoder and the pack parser
   already meet.
5. **Say when you overrule one**, and why. A recommendation declined with a
   reason recorded is a decision; declined silently it is an oversight.

### What good findings look like

A finding is a `file:line`, what goes wrong, and **a concrete trigger** — the
bytes, not "if the input were malformed". Anything that cannot name the
trigger is a suspicion, and should say so.

The reviews have earned their keep on things nobody would have found by
reading: a 200-byte `pack.json` that bricked the board through cJSON's
recursion against an 8 KB stack, clamps that inverted on `1e999` and landed
`period_ms` on exactly the value the range existed to forbid, and an
`openness: 0` that passed every per-field check and blanked the panel. None of
those are visible in a diff. Neither are the two cases where a reviewer
corrected the author.

## Development Rules
- 3-phase approval workflow: Requirements → Design → Tasks → Implementation
- Human review required each phase; use `-y` only for intentional fast-track
- Keep steering current and verify alignment with `/kiro-spec-status`
- Follow the user's instructions precisely, and within that scope act autonomously: gather the necessary context and complete the requested work end-to-end in this run, asking questions only when essential information is missing or the instructions are critically ambiguous.

## Steering Configuration
- Load entire `.kiro/steering/` as project memory
- Default files: `product.md`, `tech.md`, `structure.md`
- Custom files are supported (managed via `/kiro-steering-custom`)
