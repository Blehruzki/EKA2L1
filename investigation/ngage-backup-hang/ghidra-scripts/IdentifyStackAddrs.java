import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 57: the raw stack scan at the moment each fatal -5 Leave begins shows
// both cycles share an identical outer call chain, but differ in the innermost
// frames: cycle 1 (survives) has return address 0x80778969 that cycle 2
// (escalates to panic) doesn't; where cycle 1 has 0x807789b5, cycle 2 has
// 0x807789e7 at the same stack slot. All three are in bafl.dll. Identify what
// function(s) these are - real addresses are odd (Thumb return marker), so the
// call instruction is at addr-1 minus a few bytes.
public class IdentifyStackAddrs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/identify_stack_addrs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] rawAddrs = { 0x80778969L, 0x807789b5L, 0x807789e7L };
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long raw : rawAddrs) {
            long real = (raw & 1) != 0 ? raw - 1 : raw;
            Address target = toAddr(real);
            out.printf("=== raw=%s real_return=%s ===%n", Long.toHexString(raw), target);

            Function func = fm.getFunctionContaining(target);
            out.printf("containing function: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

            // Show a window of instructions around the return point, forcing
            // disassembly at a few candidate back-offsets if nothing exists yet
            // (round 52's Thumb-alignment lesson).
            boolean shown = false;
            for (long backOff : new long[] { 0, 4, 2, 6, 8 }) {
                Address a = target.subtract(backOff);
                Instruction existing = listing.getInstructionAt(a);
                if (existing != null && !shown) {
                    out.printf("[existing, back=%d] ", backOff);
                    Address cur = a;
                    for (int k = 0; k < 6 && cur.compareTo(target.add(4)) <= 0; k++) {
                        Instruction ii = listing.getInstructionAt(cur);
                        if (ii == null) break;
                        out.printf("%s: %s | ", cur, ii.toString());
                        cur = cur.add(ii.getLength());
                    }
                    out.println();
                    shown = true;
                }
            }

            if (func == null) {
                try {
                    disassemble(target.subtract(8));
                    createFunction(target.subtract(8), null);
                    func = fm.getFunctionContaining(target);
                } catch (Exception e) {
                    out.println("force-create failed: " + e.getMessage());
                }
            }

            if (func != null) {
                out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
                DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
            } else {
                out.println("raw disasm window:");
                Address winStart = target.subtract(0x20);
                InstructionIterator ins = listing.getInstructions(winStart, true);
                int count = 0;
                while (ins.hasNext() && count < 30) {
                    Instruction i = ins.next();
                    if (i.getAddress().compareTo(target.add(8)) > 0) break;
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                    count++;
                }
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
