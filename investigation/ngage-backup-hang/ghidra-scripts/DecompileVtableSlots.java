import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileVtableSlots extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_slots_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        long[] slots = { 0x804c8513L, 0x804c8545L, 0x804ccfdcL, 0x804c84bdL, 0x804ccdf4L,
                          0x804c89fbL, 0x804ccdecL, 0x804ccde4L, 0x804ce284L, 0x804cc0fbL, 0x804cc125L };
        for (long s : slots) {
            long addr = s & ~1L; // clear Thumb bit
            Address a = toAddr(addr);
            Function f = getFunctionContaining(a);
            out.println("==== slot addr 0x" + Long.toHexString(addr) + " -> " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)") + " ====");
            if (f == null) continue;
            try {
                DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("(decompile failed)");
                }
            } catch (Exception e) {
                out.println("(exception: " + e.getMessage() + ")");
            }
            out.println();
        }
        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
