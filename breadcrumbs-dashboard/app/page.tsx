"use client";

import { useEffect, useState } from "react";
import type { BreadcrumbMessage } from "@/lib/message";

export default function Home() {
  const [messages, setMessages] = useState<BreadcrumbMessage[]>([]);
  const [error, setError] = useState<string | null>(null);

  async function loadMessages() {
    try {
      const res = await fetch("/api/message");
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = (await res.json()) as BreadcrumbMessage[];
      setMessages([...data].reverse());
      setError(null);
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : "Failed to load messages");
    }
  }

  async function clearMessages() {
    try {
      const res = await fetch("/api/message", { method: "DELETE" });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      setMessages([]);
      setError(null);
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : "Failed to clear messages");
    }
  }

  useEffect(() => {
    loadMessages();
    const t = setInterval(loadMessages, 1500);
    return () => clearInterval(t);
  }, []);

  return (
    <main style={{ fontFamily: "sans-serif", padding: 28, maxWidth: 820 }}>
      <h1 style={{ marginBottom: 6 }}>🍞 Breadcrumbs Live Dashboard</h1>
      <p style={{ marginTop: 0 }}>
        Messages relayed from the trail to the gateway and posted to this API.
      </p>

      <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
        <button onClick={loadMessages}>Refresh</button>
        <button onClick={clearMessages}>Clear messages</button>
        {error && <span style={{ color: "crimson" }}>{error}</span>}
      </div>

      <div style={{ marginTop: 18 }}>
        {messages.length === 0 ? (
          <p>No messages yet…</p>
        ) : (
          messages.map((m, i) => (
            <div
              key={`${m.receivedAt ?? "t"}-${i}`}
              style={{
                border: "1px solid #ccc",
                borderRadius: 12,
                padding: 14,
                marginBottom: 10,
                background:
                  (m.type ?? "MSG").toUpperCase() === "SOS" ? "#fff5f5" : "#fff",
                color: "#1a1a1a",
              }}
            >
              <div style={{ display: "flex", justifyContent: "space-between" }}>
                <b>
                  {(m.type ?? "MSG").toUpperCase()} — {m.crumb_id}
                </b>
                <small style={{ color: "#555" }}>{m.receivedAt ?? ""}</small>
              </div>
              <p style={{ marginBottom: 8, color: "#1a1a1a" }}>{m.message}</p>
              <small style={{ color: "#555" }}>
                Hop count: {m.hop_count ?? "-"}
                {m.delay_ms != null && m.delay_ms > 0 && ` · Delay: ${m.delay_ms} ms`}
              </small>
            </div>
          ))
        )}
      </div>
    </main>
  );
}
