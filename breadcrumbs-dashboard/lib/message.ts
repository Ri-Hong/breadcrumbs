export type BreadcrumbMessage = {
  id?: string; // optional message id from sender (used for dedupe)
  crumb_id: string;
  type?: string; // e.g., "SOS" | "MSG"
  message: string;
  hop_count?: number;
  delay_ms?: number; // delay applied by crumb before relaying (ms)
  receivedAt?: string; // server adds this
};
