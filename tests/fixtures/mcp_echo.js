#!/usr/bin/env node
// Minimal MCP stdio server used by tests/dev_api_test.sh: tools "echo" and "add".
const rl = require('readline').createInterface({ input: process.stdin });
const send = (m) => process.stdout.write(JSON.stringify(m) + '\n');
rl.on('line', (line) => {
  let m; try { m = JSON.parse(line); } catch { return; }
  if (m.id === undefined) return;   // notification
  if (m.method === 'initialize') return send({ jsonrpc: '2.0', id: m.id, result: { protocolVersion: '2025-06-18', capabilities: { tools: {} }, serverInfo: { name: 'echo-test', version: '1.0' }, instructions: 'Test server.' } });
  if (m.method === 'tools/list') return send({ jsonrpc: '2.0', id: m.id, result: { tools: [
    { name: 'echo', description: 'Echo text back', inputSchema: { type: 'object', properties: { text: { type: 'string' } }, required: ['text'] } },
    { name: 'add', description: 'Add two numbers', inputSchema: { type: 'object', properties: { a: { type: 'number' }, b: { type: 'number' } } } } ] } });
  if (m.method === 'tools/call') {
    const a = m.params.arguments || {};
    const text = m.params.name === 'add' ? String((a.a || 0) + (a.b || 0)) : 'echo: ' + a.text;
    console.error('called ' + m.params.name);
    return send({ jsonrpc: '2.0', id: m.id, result: { content: [{ type: 'text', text }] } });
  }
  send({ jsonrpc: '2.0', id: m.id, error: { code: -32601, message: 'Method not found' } });
});
