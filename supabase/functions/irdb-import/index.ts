// supabase/functions/irdb-import/index.ts
// Supabase Edge Function – Import IR commands từ IRDB
// Deploy: supabase functions deploy irdb-import

import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
};

// ─── IRDB API Base ────────────────────────────────────────────
// IRDB là open source IR database: https://github.com/probonopd/irdb
// Dùng mirror API hoặc file-based từ GitHub raw content
const IRDB_BASE =
  "https://raw.githubusercontent.com/probonopd/irdb/master/codes";

interface IRDBEntry {
  functionName: string;
  protocol: string;
  device: number;
  subdevice: number;
  function: number;
}

interface ImportRequest {
  brand: string;         // e.g., "LG"
  deviceType: string;    // e.g., "TV"
  model?: string;        // optional specific model
  deviceId: string;      // UUID của ESP32 device
  selectedFunctions?: string[]; // Nếu null → import tất cả
}

serve(async (req: Request) => {
  // CORS preflight
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  try {
    // Xác thực user qua JWT
    const authHeader = req.headers.get("Authorization");
    if (!authHeader) {
      return new Response(
        JSON.stringify({ error: "Missing authorization header" }),
        { status: 401, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const supabase = createClient(
      Deno.env.get("SUPABASE_URL") ?? "",
      Deno.env.get("SUPABASE_ANON_KEY") ?? "",
      { global: { headers: { Authorization: authHeader } } }
    );

    const { data: { user }, error: authError } = await supabase.auth.getUser();
    if (authError || !user) {
      return new Response(
        JSON.stringify({ error: "Unauthorized" }),
        { status: 401, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const body: ImportRequest = await req.json();
    const { brand, deviceType, deviceId, selectedFunctions } = body;

    if (!brand || !deviceType || !deviceId) {
      return new Response(
        JSON.stringify({ error: "Missing required fields: brand, deviceType, deviceId" }),
        { status: 400, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // ─── Bước 1: Tìm file CSV từ IRDB ──────────────────────────
    // IRDB structure: /codes/{Category}/{Brand}/{Model}.csv
    const brandEncoded = encodeURIComponent(brand);
    const typeEncoded = encodeURIComponent(deviceType);

    // Lấy danh sách files từ GitHub API
    const githubApiUrl = `https://api.github.com/repos/probonopd/irdb/contents/codes/${typeEncoded}/${brandEncoded}`;
    const githubRes = await fetch(githubApiUrl, {
      headers: { "User-Agent": "SmartRemote-App/1.0" },
    });

    if (!githubRes.ok) {
      return new Response(
        JSON.stringify({
          error: `Brand "${brand}" not found in IRDB for device type "${deviceType}"`,
          available_url: githubApiUrl,
        }),
        { status: 404, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const files: Array<{ name: string; download_url: string }> = await githubRes.json();
    const csvFile = files[0]; // Lấy file đầu tiên (hoặc có thể cho user chọn)

    if (!csvFile) {
      return new Response(
        JSON.stringify({ error: "No IR data files found" }),
        { status: 404, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // ─── Bước 2: Download và parse CSV ─────────────────────────
    const csvRes = await fetch(csvFile.download_url);
    const csvText = await csvRes.text();
    const entries = parseIRDBCsv(csvText);

    // ─── Bước 3: Filter nếu user chọn specific functions ───────
    const filtered = selectedFunctions
      ? entries.filter((e) => selectedFunctions.includes(e.functionName))
      : entries;

    if (filtered.length === 0) {
      return new Response(
        JSON.stringify({ error: "No matching IR commands found" }),
        { status: 404, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // ─── Bước 4: Convert sang ir_commands schema ────────────────
    const irCommands = filtered.map((entry) => ({
      user_id: user.id,
      device_id: deviceId,
      name: formatFunctionName(entry.functionName),
      protocol: entry.protocol,
      address: entry.device,
      command: entry.function,
      raw_data: null, // IRDB dùng protocol-based, không phải raw
      frequency: 38000,
      source: "irdb",
      irdb_id: `${brand}_${deviceType}_${entry.functionName}`,
      irdb_brand: brand,
      irdb_device: deviceType,
    }));

    // ─── Bước 5: Bulk insert vào Supabase ──────────────────────
    const { data: inserted, error: insertError } = await supabase
      .from("ir_commands")
      .insert(irCommands)
      .select("id, name, protocol");

    if (insertError) {
      return new Response(
        JSON.stringify({ error: insertError.message }),
        { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    // ─── Bước 6: Trả về kết quả ────────────────────────────────
    return new Response(
      JSON.stringify({
        success: true,
        imported_count: inserted?.length ?? 0,
        brand,
        device_type: deviceType,
        source_file: csvFile.name,
        commands: inserted,
        available_functions: entries.map((e) => e.functionName),
      }),
      {
        status: 200,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      }
    );
  } catch (error) {
    return new Response(
      JSON.stringify({ error: "Internal server error", detail: String(error) }),
      { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  }
});

// ─── Helper: Parse IRDB CSV ──────────────────────────────────
// IRDB CSV format: functionname,protocol,device,subdevice,function
function parseIRDBCsv(csv: string): IRDBEntry[] {
  const lines = csv.trim().split("\n");
  const entries: IRDBEntry[] = [];

  for (let i = 1; i < lines.length; i++) { // Skip header
    const cols = lines[i].split(",");
    if (cols.length < 5) continue;

    entries.push({
      functionName: cols[0].trim(),
      protocol: cols[1].trim(),
      device: parseInt(cols[2]) || 0,
      subdevice: parseInt(cols[3]) || -1,
      function: parseInt(cols[4]) || 0,
    });
  }

  return entries;
}

// ─── Helper: Format tên đẹp hơn ─────────────────────────────
// "power" → "Power" | "vol+" → "Vol +" | "ch_up" → "Ch Up"
function formatFunctionName(raw: string): string {
  return raw
    .replace(/_/g, " ")
    .replace(/\+/g, " +")
    .replace(/\-/g, " -")
    .split(" ")
    .map((w) => w.charAt(0).toUpperCase() + w.slice(1).toLowerCase())
    .join(" ")
    .trim();
}
