const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const outDir = path.join(root, 'my_astro_ai', 'all_in_one_data');
const extDir = path.join(outDir, 'ext');

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(path.join(extDir, 'skills'), { recursive: true });
fs.mkdirSync(path.join(extDir, 'plugins'), { recursive: true });

const catalog = JSON.parse(fs.readFileSync(path.join(root, 'library', 'catalog.json'), 'utf8'));

// 1. Build mcp.json
const mcpJson = { mcpServers: {} };
const libraryReceipt = { schema: 1, items: {} };
const pluginsJson = {};

for (const item of catalog.items || []) {
  if (item.kind === 'mcp') {
    const serverConf = {};
    if (item.mcp) {
      if (item.mcp.type === 'http' || item.mcp.url) {
        serverConf.url = item.mcp.url;
        serverConf.type = 'http';
      } else {
        if (item.mcp.command) serverConf.command = item.mcp.command;
        if (item.mcp.args) serverConf.args = item.mcp.args;
        if (item.mcp.env) serverConf.env = item.mcp.env;
      }
    } else if (item.command) {
      serverConf.command = item.command;
      if (item.args) serverConf.args = item.args;
      if (item.env) serverConf.env = item.env;
    }
    mcpJson.mcpServers[item.id] = serverConf;
    libraryReceipt.items[item.id] = {
      kind: 'mcp',
      name: item.name || item.id,
      source: 'community',
      installed_at: Date.now()
    };
  }
}

// 2. Copy Skills
const skillsSrc = path.join(root, 'library', 'skills');
if (fs.existsSync(skillsSrc)) {
  for (const sk of fs.readdirSync(skillsSrc)) {
    const sPath = path.join(skillsSrc, sk);
    if (!fs.statSync(sPath).isDirectory()) continue;
    const dest = path.join(extDir, 'skills', sk);
    fs.cpSync(sPath, dest, { recursive: true });
    libraryReceipt.items[sk] = {
      kind: 'skill',
      name: sk,
      source: 'community',
      installed_at: Date.now()
    };
  }
}

// 3. Copy Plugins
const pluginsSrc = path.join(root, 'library', 'plugins');
if (fs.existsSync(pluginsSrc)) {
  for (const pl of fs.readdirSync(pluginsSrc)) {
    const pPath = path.join(pluginsSrc, pl);
    if (!fs.statSync(pPath).isDirectory()) continue;
    const dest = path.join(extDir, 'plugins', pl);
    fs.cpSync(pPath, dest, { recursive: true });
    pluginsJson[pl] = { enabled: true };
    libraryReceipt.items[pl] = {
      kind: 'plugin',
      name: pl,
      source: 'community',
      installed_at: Date.now()
    };
  }
}

// Write out JSON files
fs.writeFileSync(path.join(extDir, 'mcp.json'), JSON.stringify(mcpJson, null, 2) + '\n');
fs.writeFileSync(path.join(extDir, 'plugins.json'), JSON.stringify(pluginsJson, null, 2) + '\n');
fs.writeFileSync(path.join(extDir, 'library.json'), JSON.stringify(libraryReceipt, null, 2) + '\n');

// Copy keys and gateway
fs.copyFileSync(path.join(root, '.astro_keys.env'), path.join(outDir, '.astro_keys.env'));
if (fs.existsSync(path.join(root, '.workspace.env'))) {
  const envContent = fs.readFileSync(path.join(root, '.workspace.env'), 'utf8');
  const gwLines = envContent.split('\n').filter(l => l.startsWith('LLM_')).join('\n') + '\n';
  fs.writeFileSync(path.join(outDir, 'gateway.env'), gwLines);
}

console.log('Successfully prepared all-in-one personal data:');
console.log(`- MCP Servers: ${Object.keys(mcpJson.mcpServers).length}`);
console.log(`- Skills: ${fs.readdirSync(path.join(extDir, 'skills')).length}`);
console.log(`- Plugins: ${fs.readdirSync(path.join(extDir, 'plugins')).length}`);
console.log(`- Total Receipt Items: ${Object.keys(libraryReceipt.items).length}`);
