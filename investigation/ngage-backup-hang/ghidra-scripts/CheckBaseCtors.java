import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class CheckBaseCtors extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/check_base_ctors.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        long[] targets = { 0x803a2e18L, 0x803a6adcL };
        for (long t : targets) {
            Address a = toAddr(t);
            Function f = getFunctionContaining(a);
            out.println("==== 0x" + Long.toHexString(t) + " -> " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)") + " ====");
            if (f != null) {
                DecompileResults res = decomp.decompileFunction(f, 20, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                }
            } else {
                Instruction instr = getInstructionAt(a);
                if (instr == null) { try { disassemble(a); instr = getInstructionAt(a); } catch (Exception e) {} }
                for (int i = 0; i < 10 && instr != null; i++) {
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
