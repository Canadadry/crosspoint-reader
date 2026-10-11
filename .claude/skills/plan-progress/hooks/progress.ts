import type { Item } from '../types'

export function progressParse(text: string): Item[] {
  return text
    .split('\n')
    .map(line => /^( *)- \[([ x])\] (.*)$/.exec(line))
    .filter((match): match is RegExpExecArray => match !== null)
    .map(([, indent = '', mark, text = '']) => ({ depth: indent.length / 2, isDone: mark === 'x', text: text.trim() }))
}

function isLeaf(items: Item[], index: number): boolean {
  const next = items[index + 1]
  return next === undefined || next.depth <= (items[index]?.depth ?? 0)
}

export type Summary = { done: number; total: number; next: Item | undefined }

export function progressSummary(items: Item[]): Summary {
  const leaves = items.filter((_, index) => isLeaf(items, index))
  return {
    done: leaves.filter(item => item.isDone).length,
    total: leaves.length,
    next: leaves.find(item => !item.isDone),
  }
}

export function progressLine(items: Item[]): string | undefined {
  if (items.length === 0) return undefined
  const { done, total, next } = progressSummary(items)
  return `progress ${done}/${total}` + (next ? ` · next: ${next.text}` : ' · done')
}
