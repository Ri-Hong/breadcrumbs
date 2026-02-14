export type BreadcrumbMessage = {
  crumb_id: string;
  type?: string; // e.g., "SOS" | "MSG"
  message: string;
  hop_count?: number;
  receivedAt?: string; // server adds this
};
