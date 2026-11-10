# Coordinating multiple SQLite investigations

Read this when assigned a batch of bugs or authorized to coordinate workers.
Use [AGENTS.md](../AGENTS.md) for project policy and
[the investigation procedure](ai-investigation.md) for each case. This
document does not require a particular model, service, database, or fleet tool.

## Inventory and group

Build the inventory from a reproducible source listing or repository snapshot.
Record its revision or artifact set and capture time. Count and deduplicate
threads deterministically before asking a model to classify them. Use the full
identity of the original root post; an edited root has a new artifact without
becoming a new case. Preserve the latest replies and status separately.

For each case, retain the input revision/hash, current upstream status, group,
owner, baseline, candidate revision or patch hash, evidence locations,
validation status, unresolved decision, and next action. Record publication
separately from investigation and acceptance. An investigation can be complete
while the bug awaits a maintainer decision. A small batch can use a file;
larger batches may benefit from a local SQLite database with one coordinator
responsible for state transitions. Workers return reports rather than racing
to overwrite shared status. Keep credentials and bulky raw inputs separate.

Group by suspected mechanism, shared source, and likely patch interaction.
Treat model-assigned groups as hypotheses. Require report or source evidence
for an assignment; allow an explicit unknown. Similar symptoms can have
different causes, and separate symptoms can share one cause. Preserve the
initial grouping and record later splits, merges, and dependencies.

Recheck changed threads before dispatch and handoff. An assessment belongs to
the input revision it examined. Older snapshots or delayed worker reports
must not overwrite newer conclusions. A refuted or withdrawn candidate stays
withdrawn until an explicit, evidence-backed reassessment supersedes it.

## Assign bounded work

Give each worker:

- The case IDs, current evidence, pinned source, and specific question.
- Ownership of a separate checkout and output directory, with shared areas
  identified as read-only.
- Expected deliverables, report location, acceptance checks, and evidence
  references for decisive claims.
- Resource and time limits, publication authority, and the conditions for
  returning a maintainer decision or preserving an incomplete handoff.

Use less expensive models for extraction, indexing, and tentative grouping
when their output is easy to check. Require citations and uncertainty rather
than confident guesses. Assign deeper reasoning and independent review where
an error could affect correctness, compatibility, persistence, or the validity
of the investigation. Choose by demonstrated performance on the task;
model names and effort settings do not establish correctness.

Keep one implementation owner for a coupled code area. Investigate independent
questions concurrently; serialize overlapping edits or integrate them in a
separate candidate with its own validation. Two individually passing patches
can interact. When mechanisms overlap, run each case's probes against the
individual candidates and the combined result.

Budget CPU, memory, disk, broad test runners, and maintainer review capacity.
Reuse baseline results only under the equivalence conditions in the
investigation guide. Schedule performance measurements away from competing
work. Limit new dispatch when completed investigations already exceed the
capacity to review them.

## Review, reconcile, and resume

Read the artifacts, not just the worker's final assurance. Spot-check the
reproducer, controlling source path, counterexample, and decisive validation
results. A worker being idle or reporting completion does not establish that
its background tests finished.

For consequential changes, ask an independent reviewer to seek a falsifying
case, preferably with a different model or approach when practical. Supply
the reproducer, candidate, invariant, and evidence; request concrete failures
and coverage gaps. Resolve findings before calling a candidate qualified.
Neither a second model's agreement nor a broad green suite settles an
unresolved semantic choice.

Before handoff, reconcile candidate revisions, withdrawn alternatives, review
findings, upstream changes, and test receipts. Publish one concise, current
account through the authorized submission route. Track whether a reply and
its attachment were separately verified. Avoid duplicate reports from workers
and supervisors.

At a pause or resource limit, preserve the exact candidate, reproducible
commands, completed and pending checks, refutations, and next discriminating
action. Mark incomplete work resumable and unresolved decisions as awaiting
maintainer input; do not mark the bug resolved.
Do not restart expensive work when its existing evidence remains applicable.
Retain enough state for a different investigator to continue without the
original conversation.

Improve these instructions from demonstrated failures. Favor a narrow,
reusable correction over an accumulating catalog of special cases. Measure
success by trustworthy investigations and reduced maintainer review effort.
