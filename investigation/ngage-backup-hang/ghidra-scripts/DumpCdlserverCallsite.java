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

// Round 29 follow-up: 0x83482541 (cdlengine.dll) turned out to be a plain function
// epilogue - likely stale stack data, not a live call site. 0x83485fef (cdlserver.exe)
// appeared TWICE in the same stack scan, a better candidate for a genuine, active
// return address. Disassemble the function containing it.
public class DumpCdlserverCallsite extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_callsite.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address target = toAddr(0x83485fefL);
        Address winStart = toAddr(0x83485e80L);
        Address winEnd = toAddr(0x83486080L);

        clearListing(winStart, winEnd);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, winStart, winEnd, BigInteger.ONE);
        }
        disassemble(winStart);
        analyzeAll(currentProgram);

        Function func = fm.getFunctionContaining(target);
        out.println("=== function containing 0x83485fef ===");
        if (func == null) {
            out.println("no function found; dumping raw window instead");
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 150) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(winEnd) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
        } else {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
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
