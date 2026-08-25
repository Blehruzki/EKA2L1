import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmRaw extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/disasm_raw_r70_recheck2.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        long[][] ranges = {
            {0x70805278L, 0x708052E0L},
            {0x70804850L, 0x708048C0L}
        };

        for (long[] range : ranges) {
            Address start = toAddr(range[0]);
            Address end = toAddr(range[1]);
            out.printf("---- range %s - %s ----%n", start, end);

            Address a = start;
            while (a.compareTo(end) < 0) {
                Instruction ins = listing.getInstructionAt(a);
                if (ins == null) {
                    try {
                        disassemble(a);
                        ins = listing.getInstructionAt(a);
                    } catch (Exception e) {
                        out.printf("%s: disasm failed: %s%n", a, e.getMessage());
                        a = a.add(2);
                        continue;
                    }
                }
                if (ins != null) {
                    StringBuilder rawb = new StringBuilder();
                    for (int i = 0; i < ins.getLength(); i++) {
                        rawb.append(String.format("%02x ", getByte(a.add(i)) & 0xff));
                    }
                    out.printf("%s  [%s] %s%n", a, rawb.toString().trim(), ins.toString());
                    a = a.add(ins.getLength());
                } else {
                    try {
                        byte b0 = getByte(a);
                        byte b1 = getByte(a.add(1));
                        out.printf("%s  .byte 0x%02x, 0x%02x  (raw data, possible jump table)%n", a, b0 & 0xff, b1 & 0xff);
                    } catch (Exception e) {
                        out.printf("%s: read failed%n", a);
                    }
                    a = a.add(2);
                }
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
