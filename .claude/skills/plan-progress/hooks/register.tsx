import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register } from 'claude-code'

import { progressLine, progressParse, progressSummary } from './progress'

const PANE = 'plan-progress'
const items = atom({ plugin: 'plan-progress', key: 'items' } as const, [])

async function refresh($: EngineInterface): Promise<void> {
  const text = await $.fs.read('progress.md').catch(() => '')
  const parsed = progressParse(text)
  await update($, items, () => parsed)
  $.ui.status(progressLine(parsed))
  if (parsed.length > 0) void $.ui.open({ id: PANE, title: 'Progress' })
}

// Without the hook, a commit that ticks nothing would go through unnoticed.
async function installHook($: EngineInterface): Promise<void> {
  await $.process.run(['sh', 'tools/progress_check.sh', '--install']).catch(() => undefined)
}

export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    await $.command.register({ name: 'progress', description: 'Show progress.md: done, total, next item' })
    const hasProgress = await $.fs.read('progress.md').then(() => true, () => false)
    if (hasProgress) await installHook($)
    await refresh($)
    return next(e)
  })

  on('prompt.submit', async ($, e, next) => {
    if (/^\s*\/plan-pilot\b/.test(e.text)) await installHook($)
    return next(e)
  }).catch(($, e, next) => next(e))

  on('tool.call', { tool: 'Skill' }, async ($, e, next) => {
    if (e.skill === 'plan-pilot') await installHook($)
    return next(e)
  }).catch(($, e, next) => next(e))

  on('tool.call', { tool: 'Bash' }, async ($, e, next) => {
    const ran = await next(e)
    if (/\bgit\b.*\b(commit|rm|checkout|reset|merge|pull)\b/.test(e.command)) await refresh($)
    return ran
  }).catch(($, e, next) => next(e))

  on('tool.call', { tool: 'Write' }, async ($, e, next) => {
    const ran = await next(e)
    if (/(^|\/)progress\.md$/.test(e.file_path)) await refresh($)
    return ran
  }).catch(($, e, next) => next(e))

  on('tool.call', { tool: 'Edit' }, async ($, e, next) => {
    const ran = await next(e)
    if (/(^|\/)progress\.md$/.test(e.file_path)) await refresh($)
    return ran
  }).catch(($, e, next) => next(e))

  on('command.run', { command: 'progress' }, async $ => {
    await refresh($)
    return { text: progressLine(await read($, items)) ?? 'No progress.md at the root.' }
  })

  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    const { Box, Text } = $.ui.resolve(e)
    const list = await read($, items)
    if (list.length === 0) return <Text dimColor>No progress.md at the root.</Text>
    const { done, total, next } = progressSummary(list)
    return (
      <Box flexDirection="column">
        <Text bold>{done}/{total} done</Text>
        {list.map(item => (
          <Text dimColor={item.isDone} bold={item === next}>
            {'  '.repeat(item.depth)}{item.isDone ? '✓' : item === next ? '▶' : '·'} {item.text}
          </Text>
        ))}
      </Box>
    )
  })
}
