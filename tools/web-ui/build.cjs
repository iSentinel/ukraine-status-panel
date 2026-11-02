/** Build-time minification only; this tool is never included in firmware. */
const fs = require("node:fs");
const {minify} = require("terser");
const version = require("terser/package.json").version;

/** Keep output reproducible and leave DOM/API property names unchanged. */
async function main() {
  if (version !== "5.44.0") throw new Error("Run npm ci in tools/web-ui");
  const source = fs.readFileSync(0, "utf8");
  const result = await minify(source, {
    ecma: 2020,
    module: true,
    compress: {passes: 2},
    mangle: {toplevel: true, properties: {regex: /^__usp/, keep_quoted: true}},
    format: {comments: false, ascii_only: false},
  });
  process.stdout.write(result.code);
}

main().catch(error => { process.stderr.write(error.message + "\n"); process.exitCode = 1; });
