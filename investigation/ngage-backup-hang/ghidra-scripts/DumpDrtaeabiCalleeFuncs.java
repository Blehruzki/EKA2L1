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

// Round 52: cdlserver.exe's two call sites into drtaeabi.dll (via local thunks)
// land at 0x803954e8 and 0x80395580 - confirmed via raw-byte thunk-target reads,
// exactly matching the live PC captures. Decompile both drtaeabi.dll functions to
// see what they actually compute:
//   0x803954e8 args: r0=0x7006d8 r1=0 r2=0x80397398 r3=0
//   0x80395580 args: r0=0xfffffff1 r1=0 r2=0x4d5241 r3=0x4d5241
public class DumpDrtaeabiCalleeFuncs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/drtaeabi_callee_funcs.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        long[] targets = { 0x803954e8L, 0x80395580L };

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long t : targets) {
            Address target = toAddr(t);
            out.printf("=== target %s ===%n", target);

            Function func = fm.getFunctionContaining(target);
            out.printf("containing function: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

            if (func == null) {
                disassemble(target);
                createFunction(target, null);
                func = fm.getFunctionContaining(target);
                out.printf("after forcing: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "still none");
            }

            if (func != null) {
                out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
                out.println("--- disassembly ---");
                InstructionIterator ins = listing.getInstructions(func.getBody(), true);
                int printed = 0;
                while (ins.hasNext() && printed < 300) {
                    Instruction i = ins.next();
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                    printed++;
                }

                out.println("--- decompiled ---");
                DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
            } else {
                out.println("dumping raw disassembly window instead");
                Address winStart = target.subtract(0x10);
                Address winEnd = target.add(0x200);
                InstructionIterator ins = listing.getInstructions(winStart, true);
                int count = 0;
                while (ins.hasNext() && count < 200) {
                    Instruction i = ins.next();
                    if (i.getAddress().compareTo(winEnd) > 0) break;
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
