import { expect, test } from 'claude-code/testing'
import { progressLine, progressParse, progressSummary } from './progress'

const TEXT = `- [x] Phase 1 — Groundwork
  - [x] 1. Fix slipgate refusal pulse
  - [x] 2. Select button
- [ ] Phase 2 — Jump mode
  - [ ] 3. Jump mode
    - [x] chunk A
    - [ ] chunk B
  - [ ] 4. Aim mode
`

test('parses depth, state and text', () => {
  expect(progressParse(TEXT)[5]).toEqual({ depth: 2, isDone: true, text: 'chunk A' })
})

test('counts leaves only and points at the first unticked one', () => {
  const summary = progressSummary(progressParse(TEXT))
  expect([summary.done, summary.total, summary.next?.text]).toEqual([3, 5, 'chunk B'])
})

test('a missing or empty file has no status line', () => {
  expect(progressLine(progressParse(''))).toBeUndefined()
  expect(progressLine(progressParse('- [x] a\n'))).toBe('progress 1/1 · done')
})
