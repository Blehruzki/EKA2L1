import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileAllVtableSlots2 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_cbc78_slots_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        long[] slots = { 0x804cbc37L, 0x804cbc4dL, 0x804ccfccL, 0x804cbc5dL,
                          0x804cbc65L, 0x804cbc69L, 0x804cabbdL, 0x804cbf01L };
        for (int idx = 0; idx < slots.length; idx++) {
            long s = slots[idx] & ~1L;
            Address a = toAddr(s);
            Function f = getFunctionContaining(a);
            out.println("==== slot[" + idx + "] 0x" + Long.toHexString(s) + " -> " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)") + " ====");
            if (f != null) {
                try {
                    DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                    if (res != null && res.decompileCompleted()) {
                        out.println(res.getDecompiledFunction().getC());
                    } else {
                        out.println("(decompile failed)");
                    }
                } catch (Exception e) { out.println("(exception)"); }
            } else {
                // raw disasm fallback
                Instruction instr = getInstructionAt(a);
                if (instr == null) { try { disassemble(a); instr = getInstructionAt(a); } catch (Exception e) {} }
                for (int i = 0; i < 15 && instr != null; i++) {
                    out.printf("  %s: %s%n", instr.getAddress(), instr.toString());
                    instr = instr.getNext();
                }
            }
            out.println();
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
