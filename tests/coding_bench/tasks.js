// Coding benchmark for Astro AI: realistic requests, each graded by hidden tests that run the model's code.
// lang: js (node), py (python3), cpp (g++ -std=c++17). `fn` is the name the code must define.
module.exports = [
  {
    id: 'js-duration', lang: 'js', fn: 'parseDuration',
    prompt: 'Write a JavaScript function parseDuration(text) that turns strings like "1h30m", "45s", "2h 5m 10s" or "90m" into a number of seconds. Units are h, m and s, in that order, each at most once, with optional spaces between parts. Throw an Error for anything invalid, such as "", "5x", "1m1h" or "h".',
    test: `
const eq=(a,b,m)=>{if(a!==b)throw new Error(m+': got '+a+' want '+b)};
const bad=(s)=>{let t=false;try{parseDuration(s)}catch(e){t=true}if(!t)throw new Error('no throw for '+JSON.stringify(s))};
eq(parseDuration('1h30m'),5400,'1h30m');eq(parseDuration('45s'),45,'45s');eq(parseDuration('2h 5m 10s'),7510,'2h 5m 10s');eq(parseDuration('90m'),5400,'90m');eq(parseDuration('0s'),0,'0s');
bad('');bad('5x');bad('1m1h');bad('h');bad('1h1h');`,
  },
  {
    id: 'js-lru', lang: 'js', fn: 'LRUCache',
    prompt: 'Implement an LRU cache in JavaScript as a class LRUCache with constructor(capacity), get(key) returning the value or -1 if missing, and put(key, value). Both operations must be O(1). get and put both count as a use. Inserting beyond capacity evicts the least recently used key.',
    test: `
const eq=(a,b,m)=>{if(a!==b)throw new Error(m+': got '+a+' want '+b)};
const c=new LRUCache(2);c.put(1,1);c.put(2,2);eq(c.get(1),1,'get1');c.put(3,3);eq(c.get(2),-1,'evict2');c.put(4,4);eq(c.get(1),-1,'evict1');eq(c.get(3),3,'g3');eq(c.get(4),4,'g4');
const d=new LRUCache(1);d.put('a',1);d.put('a',5);eq(d.get('a'),5,'update');d.put('b',2);eq(d.get('a'),-1,'cap1');
const e=new LRUCache(2);e.put(1,1);e.put(2,2);e.put(1,10);e.put(3,3);eq(e.get(2),-1,'put-refresh');eq(e.get(1),10,'kept1');`,
  },
  {
    id: 'js-deepequal', lang: 'js', fn: 'deepEqual',
    prompt: 'Write a JavaScript function deepEqual(a, b) that returns true when two values are structurally equal. Handle primitives, NaN (equal to NaN), nested arrays and plain objects (key order does not matter), Date objects (equal when same time) and null. An array is never equal to an object.',
    test: `
const t=(v,m)=>{if(v!==true)throw new Error(m)};const f=(v,m)=>{if(v!==false)throw new Error(m)};
t(deepEqual(1,1),'num');t(deepEqual(NaN,NaN),'nan');f(deepEqual(1,'1'),'type');t(deepEqual({a:[1,{b:2}],c:null},{c:null,a:[1,{b:2}]}),'nested');
f(deepEqual({a:1},{a:1,b:undefined}),'extra key');f(deepEqual([1,2],{0:1,1:2}),'arr vs obj');t(deepEqual(new Date(5),new Date(5)),'date');f(deepEqual(new Date(5),new Date(6)),'date2');
f(deepEqual(null,{}),'null');f(deepEqual([1,[2]],[1,[3]]),'deep arr');t(deepEqual([],[]),'empty');f(deepEqual({a:{b:1}},{a:{b:2}}),'deep obj');`,
  },
  {
    id: 'js-csv', lang: 'js', fn: 'parseCSV',
    prompt: 'Write a JavaScript function parseCSV(text) that parses CSV text into an array of rows (arrays of strings). Follow RFC 4180: fields may be wrapped in double quotes, quoted fields can contain commas, newlines and escaped quotes written as "", and lines may end with \\n or \\r\\n. A trailing newline at the end of the text does not create an extra empty row.',
    test: `
const eq=(a,b,m)=>{if(JSON.stringify(a)!==JSON.stringify(b))throw new Error(m+': got '+JSON.stringify(a))};
eq(parseCSV('a,b\\n1,2\\n'),[['a','b'],['1','2']],'basic');eq(parseCSV('"x,y",z'),[['x,y','z']],'comma');eq(parseCSV('"he said ""hi""",2'),[['he said "hi"','2']],'quote');
eq(parseCSV('"line1\\nline2",b\\r\\nc,d'),[['line1\\nline2','b'],['c','d']],'newline');eq(parseCSV('a,,c'),[['a','','c']],'empty field');eq(parseCSV('a,b\\r\\n'),[['a','b']],'crlf trail');`,
  },
  {
    id: 'py-roman', lang: 'py', fn: 'to_roman',
    prompt: 'Write two Python functions: to_roman(n) converting an integer from 1 to 3999 to a Roman numeral string, and from_roman(s) converting a valid Roman numeral back to an int. Both must raise ValueError for invalid input (out of range numbers, non-integers, or invalid numerals such as "IIII", "IC", "VX" or "").',
    test: `
assert to_roman(1994)=="MCMXCIV", to_roman(1994)
assert to_roman(3999)=="MMMCMXCIX"
assert to_roman(4)=="IV" and to_roman(9)=="IX" and to_roman(40)=="XL"
assert from_roman("MCMXCIV")==1994 and from_roman("XLII")==42
assert all(from_roman(to_roman(i))==i for i in range(1,4000))
for bad in [0,4000,-1,2.5]:
    try:
        to_roman(bad); raise SystemExit("no ValueError for %r"%bad)
    except ValueError: pass
for bad in ["IIII","IC","VX","","ABC","MMMM"]:
    try:
        from_roman(bad); raise SystemExit("no ValueError for %r"%bad)
    except ValueError: pass`,
  },
  {
    id: 'py-intervals', lang: 'py', fn: 'merge_intervals',
    prompt: 'Write a Python function merge_intervals(intervals) that takes a list of [start, end] pairs (in any order) and returns a new sorted list where overlapping or touching intervals are merged, for example [[1,3],[2,6],[8,10],[10,12]] becomes [[1,6],[8,12]]. Do not modify the input list.',
    test: `
src=[[8,10],[1,3],[2,6],[10,12]]
copy=[x[:] for x in src]
assert merge_intervals(src)==[[1,6],[8,12]], merge_intervals(src)
assert src==copy, "input modified"
assert merge_intervals([])==[]
assert merge_intervals([[5,5]])==[[5,5]]
assert merge_intervals([[1,10],[2,3],[4,5]])==[[1,10]]
assert [list(x) for x in merge_intervals([[1,2],[3,4]])]==[[1,2],[3,4]]`,
  },
  {
    id: 'py-calc', lang: 'py', fn: 'evaluate',
    prompt: 'Write a Python function evaluate(expr) that evaluates an arithmetic expression string with +, -, *, /, parentheses, unary minus, spaces and decimal numbers, using normal operator precedence, and returns a float. Do not use eval, exec or ast.literal_eval. Raise ValueError for malformed input such as "2 +", "(1", "1 2" or "".',
    test: `
import math
def close(a,b): return math.isclose(a,b,rel_tol=1e-9,abs_tol=1e-9)
assert close(evaluate("1 + 2 * 3"),7)
assert close(evaluate("(1 + 2) * 3"),9)
assert close(evaluate("-3 + 5"),2)
assert close(evaluate("2 * -(1 + 1)"),-4)
assert close(evaluate("10 / 4"),2.5)
assert close(evaluate("8 - 3 - 2"),3)
assert close(evaluate("2.5*2"),5)
assert close(evaluate("--2"),2)
for bad in ["2 +","(1","1 2","",")","1 + * 2"]:
    try:
        evaluate(bad); raise SystemExit("no ValueError for %r"%bad)
    except ValueError: pass`,
  },
  {
    id: 'py-topk', lang: 'py', fn: 'top_k_words',
    prompt: 'Write a Python function top_k_words(text, k) that returns the k most frequent words in text as a list of (word, count) tuples. Words are case-insensitive runs of letters and apostrophes (so "Don\'t" and "don\'t" are the same word). Sort by count descending, then alphabetically for ties. If k is larger than the number of distinct words return all of them.',
    test: `
r=top_k_words("the cat and the hat. The END, and fin", 3)
assert r==[("the",3),("and",2),("cat",1)], r
assert top_k_words("Don't stop. don't!", 1)==[("don't",2)]
assert top_k_words("", 5)==[]
assert top_k_words("b a", 10)==[("a",1),("b",1)]`,
  },
  {
    id: 'cpp-dijkstra', lang: 'cpp', fn: 'shortestPaths',
    prompt: 'Write a C++17 function std::vector<long long> shortestPaths(int n, const std::vector<std::tuple<int,int,int>>& edges, int src) that returns the shortest distance from src to every node 0..n-1 in a directed graph with non-negative weights (each edge is from, to, weight). Unreachable nodes get -1. Use Dijkstra with a priority queue. Include all needed headers.',
    test: `
#include <cassert>
#include <cstdio>
int main(){
  std::vector<std::tuple<int,int,int>> e={{0,1,4},{0,2,1},{2,1,2},{1,3,1},{2,3,5}};
  auto d=shortestPaths(5,e,0);
  assert(d.size()==5);assert(d[0]==0);assert(d[1]==3);assert(d[2]==1);assert(d[3]==4);assert(d[4]==-1);
  std::vector<std::tuple<int,int,int>> big={{0,1,2000000000},{1,2,2000000000}};
  auto b=shortestPaths(3,big,0);assert(b[2]==4000000000LL);
  auto s=shortestPaths(1,{},0);assert(s.size()==1&&s[0]==0);
  std::puts("ok");return 0;}`,
  },
  {
    id: 'cpp-wordfreq', lang: 'cpp', fn: 'splitTrim',
    prompt: 'Write a C++17 function std::vector<std::string> splitTrim(const std::string& s, char delim) that splits s on delim, trims spaces and tabs from both ends of each piece, and drops pieces that are empty after trimming. For example splitTrim(" a, b ,,c ,", \',\') returns {"a","b","c"}. Include all needed headers.',
    test: `
#include <cassert>
#include <cstdio>
int main(){
  auto v=splitTrim(" a, b ,,c ,", ',');assert((v==std::vector<std::string>{"a","b","c"}));
  assert(splitTrim("", ',').empty());assert(splitTrim(" , ,\\t", ',').empty());
  auto w=splitTrim("one", ',');assert(w.size()==1&&w[0]=="one");
  auto x=splitTrim("\\tx y\\t;z", ';');assert(x.size()==2&&x[0]=="x y"&&x[1]=="z");
  std::puts("ok");return 0;}`,
  },
  {
    id: 'py-json', lang: 'py', fn: 'parse_json', long: true,
    prompt: 'Write a JSON parser in Python from scratch, without the json module, eval or ast: a function parse_json(text) that returns the matching Python value (dict, list, str, int, float, True, False, None). Support nested objects and arrays, strings with all JSON escapes including \\uXXXX and surrogate pairs, integers, decimals and exponents, and whitespace. Raise ValueError for invalid JSON such as trailing commas, single quotes, unquoted keys, leading zeros like 012, or extra text after the value. Explain the design briefly too.',
    test: `
import json
samples=['{"a":[1,2,{"b":null}],"c":"x"}','[]','{}','"he said \\\\"hi\\\\""','-12.5e3','0','[true,false,null]','"\\\\u00e9\\\\ud83d\\\\ude00"','{"k":"line\\\\nbreak\\\\t\\\\\\\\"}','  [ 1 , [ 2 , [ 3 ] ] ]  ','1E2','0.5']
for s in samples:
    got=parse_json(s); want=json.loads(s)
    assert got==want and type(got)==type(want), (s,got,want)
for bad in ['[1,]','{"a":1,}',"{'a':1}",'{a:1}','012','[1] x','','"abc','[1 2]','tru','-']:
    try:
        parse_json(bad); raise SystemExit("no ValueError for %r"%bad)
    except ValueError: pass`,
  },
  {
    id: 'js-template', lang: 'js', fn: 'render', long: true,
    prompt: 'Write a small Mustache-style template engine in JavaScript: a function render(template, data). Support {{name}} with HTML escaping of & < > " and \', {{{name}}} without escaping, dotted names like {{user.name}}, sections {{#items}}...{{/items}} that repeat for arrays (inside, names resolve against the current item first, then outer data, and {{.}} is the item itself), show once for truthy non-array values and hide for falsy values or empty arrays, and inverted sections {{^items}}...{{/items}}. Missing names render as an empty string. Throw an Error for an unclosed section. Explain how it works briefly too.',
    test: `
const eq=(a,b,m)=>{if(a!==b)throw new Error(m+': got '+JSON.stringify(a))};
eq(render('Hi {{name}}!',{name:'<b>Al & "Bo"</b>'}),'Hi &lt;b&gt;Al &amp; &quot;Bo&quot;&lt;/b&gt;!','escape');
eq(render('{{{h}}}',{h:'<i>x</i>'}),'<i>x</i>','triple');
eq(render('{{user.name}}/{{user.age}}',{user:{name:'Zo',age:0}}),'Zo/0','dotted and zero');
eq(render('{{#list}}[{{.}}]{{/list}}',{list:[1,2,3]}),'[1][2][3]','dot');
eq(render('{{#people}}{{name}}@{{site}} {{/people}}',{site:'S',people:[{name:'a'},{name:'b',site:'T'}]}),'a@S b@T ','scope');
eq(render('{{#ok}}yes{{/ok}}{{^ok}}no{{/ok}}',{ok:false}),'no','inverted');
eq(render('{{#e}}x{{/e}}{{^e}}empty{{/e}}',{e:[]}),'empty','empty array');
eq(render('{{#o}}{{v}}{{/o}}',{o:{v:'in'}}),'in','object section');
eq(render('[{{missing}}]',{}),'[]','missing');
eq(render('{{#a}}{{#b}}{{c}}{{/b}}{{/a}}',{a:[{b:[{c:1},{c:2}]},{b:[{c:3}]}]}),'123','nested');
let threw=false;try{render('{{#a}}x',{a:1})}catch(e){threw=true}if(!threw)throw new Error('no throw for unclosed');`,
  },
];
