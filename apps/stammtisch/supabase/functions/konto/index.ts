// STAMMTISCH – Konto-Funktion (Supabase Edge Function „konto“)
//
// Legt für ein neues Gerät eine Anmeldung an – aber nur für jemanden mit gültigem Einladungs- oder Gerätecode.
// So entstehen ohne Einladung keine Konten. Der Beitritt selbst passiert danach in der Datenbank
// (join_group / link_device), mit genau dieser Anmeldung.
//
// Die Anmeldung besteht aus einer erfundenen Adresse und einem langen Zufallspasswort. Beides merkt sich nur das Gerät;
// niemand muss sich etwas ausdenken oder eine E-Mail bestätigen. Den geheimen Schlüssel (SUPABASE_SERVICE_ROLE_KEY)
// stellt Supabase dieser Funktion selbst bereit – er steht nirgends im Quelltext.
import { createClient } from "jsr:@supabase/supabase-js@2";

const cors = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

function json(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { ...cors, "Content-Type": "application/json; charset=utf-8" },
  });
}

function randomPassword(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: cors });
  if (req.method !== "POST") return json(405, { error: "Nur POST." });

  let code = "";
  try {
    code = String((await req.json()).code ?? "");
  } catch (_e) {
    // kein gültiges JSON: unten als ungültiger Code behandelt
  }
  code = code.toUpperCase().replace(/[^A-Z0-9]/g, "");
  if (code.length < 8 || code.length > 12) return json(403, { error: "Dieser Code gilt nicht (mehr)." });

  const admin = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!, {
    auth: { persistSession: false, autoRefreshToken: false },
  });

  const { data: valid, error: checkError } = await admin.rpc("code_valid", { p_code: code });
  if (checkError) {
    console.error("code_valid:", checkError.message);
    return json(500, { error: "Der Code ließ sich gerade nicht prüfen. Bitte gleich nochmal versuchen." });
  }
  if (!valid) return json(403, { error: "Dieser Code gilt nicht (mehr)." });

  const email = `geraet-${crypto.randomUUID()}@stammtisch.invalid`;
  const password = randomPassword();
  const { error } = await admin.auth.admin.createUser({ email, password, email_confirm: true });
  if (error) {
    console.error("createUser:", error.message);
    return json(500, { error: "Die Anmeldung ließ sich nicht anlegen. Bitte gleich nochmal versuchen." });
  }
  return json(200, { email, password });
});
