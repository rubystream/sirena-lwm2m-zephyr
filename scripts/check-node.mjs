const major = Number(process.version.slice(1).split(".")[0]);

if (major !== 24) {
  console.error(`Unsupported Node.js ${process.version}; expected major version 24 (>=24.0.0 <25.0.0).`);
  process.exit(1);
}

console.log(`Node.js ${process.version} OK`);
