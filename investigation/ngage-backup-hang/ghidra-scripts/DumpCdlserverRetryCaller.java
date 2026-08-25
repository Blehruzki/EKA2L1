import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 37 continued: found a second, previously-unknown caller of FUN_83485e24
// at 0x83485e86, bypassing the known wrapper at 0x83485fde entirely. Dump its
// containing function to see if THIS is the actual retry loop and whether it
// checks any return/error value.
public class DumpCdlserverRetryCaller extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_retry_caller.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address target = toAddr(0x83485e86L);
        Address winStart = toAddr(0x83485e68L);
        Address winEnd = toAddr(0x83485fe0L);

        clearListing(winStart, winEnd);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, winStart, winEnd, BigInteger.ONE);
        }
        disassemble(winStart);
        disassemble(target);
        if (fm.getFunctionContaining(target) == null) {
            try {
                createFunction(target, null);
            } catch (Exception e) {
            }
        }

        Function func = fm.getFunctionContaining(target);
        out.printf("=== function containing 0x%s ===%n", target);
        if (func == null) {
            out.println("no function found; dumping raw window instead");
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 300) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(winEnd) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
        } else {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            int printed = 0;
            while (ins.hasNext() && printed < 500) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                printed++;
            }

            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
