export type Item = { depth: number; isDone: boolean; text: string }

declare module 'claude-code' {
  interface PluginState {
    'plan-progress': { items: Item[] }
  }
}
