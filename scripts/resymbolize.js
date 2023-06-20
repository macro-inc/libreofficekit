const { execSync } = require("child_process");
const path = require("path");
const debugDir =
  process.argv[2] || path.join(__dirname, "../libreoffice-core/workdir/debug");

let llvmSymbolizer;

["llvm-symbolizer-14", "llvm-symbolizer-15", "llvm-symbolizer-16"].some(
  (command) => {
    try {
      execSync(`${command} --version`);
      llvmSymbolizer = command;
      return true;
    } catch (e) {
      return false;
    }
  }
);

let stdinData = "";

process.stdin.on("data", function (chunk) {
  stdinData += chunk;
});

process.stdin.on("end", function () {
  let symbolSuffix = ".debug";
  if (stdinData.includes(".exe") || stdinData.includes(".dll")) {
    symbolSuffix = ".pdb";
  } else if (stdinData.includes(".dylib") || stdinData.includes(" Framework")) {
    symbolSuffix = ".dSYM";
  }

  const re = /\[\+(0x[a-fA-F0-9]+)\]\(([^)]+)\)/g;
  const matches = [...stdinData.matchAll(re)];

  let groups = matches.reduce((acc, match) => {
    const offset = match[1];
    const file = match[2];

    if (!acc[file]) {
      acc[file] = [];
    }

    acc[file].push(offset);

    return acc;
  }, {});

  for (let file in groups) {
    const offsets = groups[file];
    const debugFile = `${debugDir}/${file}${symbolSuffix}`;

    const command = `${llvmSymbolizer} -Cfape ${debugFile} ${offsets.join(
      " "
    )}`;
    const result = execSync(command).toString().split("\n\n");

    for (let i = 0; i < offsets.length; i++) {
      const offset = offsets[i];
      const symbolizedLine = `${file}+${result[i]}`;
      const regex = new RegExp(`\\[\\+${offset}\\]\\(${file}\\)`, "g");
      stdinData = stdinData.replace(regex, symbolizedLine);
    }
  }

  process.stdout.write(stdinData);
});
