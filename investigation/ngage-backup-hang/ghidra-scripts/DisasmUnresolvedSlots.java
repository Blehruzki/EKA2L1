import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DisasmUnresolvedSlots extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/unresolved_slots_disasm.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] slots = { 0x804c8544L, 0x804ccfdcL, 0x804ccdf4L, 0x804c89faL, 0x804ccdecL, 0x804ccde4L, 0x804ce284L };
        for (long s : slots) {
            out.println("==== 0x" + Long.toHexString(s) + " ====");
            Address a = toAddr(s);
            Instruction instr = getInstructionAt(a);
            if (instr == null) {
                try {
                    disassemble(a);
                    instr = getInstructionAt(a);
                } catch (Exception e) {
                    out.println("  disasm failed: " + e.getMessage());
                }
            }
            for (int i = 0; i < 10 && instr != null; i++) {
                out.printf("  %s: %s%n", instr.getAddress(), instr.toString());
                instr = instr.getNext();
            }
            out.println();
        }
        out.close();
        println("WROTE " + outPath);
    }
}
