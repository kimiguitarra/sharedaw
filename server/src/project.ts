// プロジェクト JSON の解釈（スコープ単位の比較、参照している実体のハッシュ）。
// アプリ側の collab::ProjectDiff と同じ考え方: スコープ = トラック / テンポ / 拍子 / コード。

import { Validator } from "@cfworker/json-schema";
import schema from "../../shared/schema/project.schema.json";

export interface ProjectJson {
  schemaVersion: number;
  projectId: string;
  name: string;
  tempoTrack: { id: string };
  meterTrack: { id: string };
  chordTrack: { id: string };
  markerTrack?: { id: string };
  master?: { id: string };
  keyTrack?: { id: string; events?: unknown[] };
  tracks: Array<{
    id: string;
    type: "midi" | "audio" | "bus";
    render?: { audioHash: string };
    clips: Array<{ audioHash?: string }>;
    instrument?: { kind?: string; id?: string; params?: { pads?: Array<{ audioHash?: string } | null> } };
  }>;
}

const validator = new Validator(schema as object, "7", false);

export function validateProject(data: unknown): string | null {
  const result = validator.validate(data);

  if (result.valid) return null;

  const first = result.errors[result.errors.length - 1];
  return first ? `${first.instanceLocation}: ${first.error}` : "invalid";
}

/** キーをソートした決定的な JSON 文字列（内容比較用）。 */
export function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;

  if (value !== null && typeof value === "object") {
    const keys = Object.keys(value as object).sort();
    return `{${keys.map((k) => `${JSON.stringify(k)}:${canonical((value as Record<string, unknown>)[k])}`).join(",")}}`;
  }

  return JSON.stringify(value);
}

export type ScopeKind = "tempo" | "meter" | "chord" | "marker" | "key" | "master" | "track";

/** スコープ ID → 内容（比較用の正規化文字列）。 */
export function scopes(p: ProjectJson): Map<string, { kind: ScopeKind; content: string }> {
  const m = new Map<string, { kind: ScopeKind; content: string }>();
  m.set(p.tempoTrack.id, { kind: "tempo", content: canonical(p.tempoTrack) });
  m.set(p.meterTrack.id, { kind: "meter", content: canonical(p.meterTrack) });
  m.set(p.chordTrack.id, { kind: "chord", content: canonical(p.chordTrack) });

  // マーカートラック（古いプロジェクトにはない。空のものは「ない」と同じに扱う）
  if (p.markerTrack && (p.markerTrack as { events?: unknown[] }).events?.length)
    m.set(p.markerTrack.id, { kind: "marker", content: canonical(p.markerTrack) });

  // キートラック（空のときは JSON に出ないので、あるときだけ）
  if (p.keyTrack && p.keyTrack.events?.length) m.set(p.keyTrack.id, { kind: "key", content: canonical(p.keyTrack) });

  // マスター（既定値のときは JSON に出ないので、あるときだけ）
  if (p.master) m.set(p.master.id, { kind: "master", content: canonical(p.master) });

  for (const t of p.tracks) m.set(t.id, { kind: "track", content: canonical(t) });

  return m;
}

export interface ScopeChange {
  id: string;
  kind: ScopeKind;
  existedInParent: boolean;
  deleted: boolean;
}

/** 親リビジョン → 新しいリビジョンで変わったスコープ。 */
export function changedScopes(parent: ProjectJson | null, next: ProjectJson): ScopeChange[] {
  const before = parent ? scopes(parent) : new Map<string, { kind: ScopeKind; content: string }>();
  const after = scopes(next);
  const result: ScopeChange[] = [];

  for (const [id, s] of after) {
    const b = before.get(id);
    if (!b || b.content !== s.content) result.push({ id, kind: s.kind, existedInParent: !!b, deleted: false });
  }

  for (const [id, s] of before) {
    if (!after.has(id)) result.push({ id, kind: s.kind, existedInParent: true, deleted: true });
  }

  return result;
}

/** 参照しているオーディオ実体（録音・読み込み・バウンス・内蔵サンプラーのパッド）のハッシュ。 */
export function referencedBlobs(p: ProjectJson): string[] {
  const hashes = new Set<string>();

  for (const t of p.tracks) {
    if (t.render?.audioHash) hashes.add(t.render.audioHash);
    for (const c of t.clips) if (c.audioHash) hashes.add(c.audioHash);

    const pads = t.instrument?.id === "builtin.sampler" ? t.instrument.params?.pads : undefined;
    if (Array.isArray(pads)) for (const pad of pads) if (pad && typeof pad.audioHash === "string" && pad.audioHash) hashes.add(pad.audioHash);
  }

  return [...hashes];
}
