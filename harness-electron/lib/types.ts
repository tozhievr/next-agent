export type Message = {
  role: string;
  content: string | null;
  tool_calls?: { id: string; function: { name: string; arguments: string } }[];
  name?: string;
};
export type RunEvent = {
  kind: string;
  title: string;
  detail: string;
  seconds: number;
  error: boolean;
};
export type Session = {
  id?: string;
  title?: string;
  workspace?: string;
  messages: Message[];
  answer?: string;
  events: RunEvent[];
  changed: string[];
  running: boolean;
  status?: string;
  error?: string;
  input_tokens?: number;
  output_tokens?: number;
  cached_tokens?: number;
  cache_write_tokens?: number;
  tool_calls?: number;
  elapsed?: number;
  cost?: number;
  cost_reported?: boolean;
  last_check?: {
    checked_revision: number;
    fresh: boolean;
    exit_code: number;
  } | null;
};
export type SessionSummary = { id: string; title: string; workspace: string };
export type Settings = { model: string; hasKey: boolean; canStoreKey: boolean };
export type Capabilities = {
  version: string;
  modules: { name: string; loaded: boolean; available: boolean }[];
};
export type Entry = { path: string; directory: boolean; name?: string };
export type Opened = {
  root: string;
  capabilities: Capabilities;
  session: Session;
};
export interface HarnessAPI {
  bootstrap(): Promise<{
    root: string;
    settings: Settings;
    session: Session;
    sessions: SessionSummary[];
    capabilities: Capabilities | null;
  }>;
  chooseWorkspace(): Promise<Opened | null>;
  newSession(): Promise<Session>;
  restoreSession(id: string): Promise<Session>;
  send(prompt: string, readOnly: boolean): Promise<Session>;
  cancel(): Promise<Session>;
  snapshot(): Promise<Session>;
  sessions(): Promise<SessionSummary[]>;
  tree(path?: string): Promise<Entry[]>;
  read(path: string): Promise<{ text: string }>;
  history(): Promise<unknown>;
  labels(): Promise<unknown>;
  settings(v: {
    model: string;
    apiKey?: string;
    clearKey?: boolean;
  }): Promise<Settings>;
  window(command: string): Promise<null>;
  selfTest(): Promise<Opened>;
}
declare global {
  interface Window {
    harness?: HarnessAPI;
  }
}
