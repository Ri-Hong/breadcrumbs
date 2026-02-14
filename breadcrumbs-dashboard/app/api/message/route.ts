import type { BreadcrumbMessage } from "@/lib/message";

// In-memory store (good for hackathon demo; resets on redeploy/cold start)
let messages: BreadcrumbMessage[] = [];

type OkResp = { status: "ok" };
type ErrResp = { error: string };

export async function GET() {
  return Response.json(messages);
}

export async function POST(request: Request) {
  const body = (await request.json()) as Partial<BreadcrumbMessage>;

  if (!body.crumb_id || !body.message) {
    return Response.json(
      { error: "Missing crumb_id or message" } satisfies ErrResp,
      { status: 400 }
    );
  }

  const id = body.id != null ? String(body.id).trim() : undefined;

  // Dedupe: if sender sent same id (e.g. retries), only store once
  if (id && messages.some((m) => m.id === id)) {
    return Response.json({ status: "ok" } satisfies OkResp);
  }

  const entry: BreadcrumbMessage = {
    ...(id && { id }),
    crumb_id: String(body.crumb_id),
    type: body.type ? String(body.type) : "MSG",
    message: String(body.message),
    hop_count:
      body.hop_count !== undefined ? Number(body.hop_count) : undefined,
    receivedAt: new Date().toISOString(),
  };

  messages.push(entry);

  if (messages.length > 200) messages = messages.slice(-200);

  return Response.json({ status: "ok" } satisfies OkResp);
}

export async function DELETE() {
  messages = [];
  return Response.json({ status: "ok" } satisfies OkResp);
}
