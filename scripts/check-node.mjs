const expected = "24.22.0";

if (process.version !== `v${expected}`) {
  console.error(`Unsupported Node.js ${process.version}; expected v${expected}.`);
  process.exit(1);
}

console.log(`Node.js ${process.version} OK`);
