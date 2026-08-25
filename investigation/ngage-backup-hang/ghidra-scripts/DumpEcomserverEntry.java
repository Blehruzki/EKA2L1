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

// Round 38: round 37 found no error-check loop in cdlserver.exe's own code - the
// repeated Leave(-15) calls are Active Object re-arming against an already-dead
// ecomserver session. Need to know why ecomserver.exe itself exits: dump its
// entry point (E32Main) to find the CActiveScheduler::Start()/Stop() structure
// and whatever decides when to stop.
public class DumpEcomserverEntry extends GhidraScript {
    private void dumpFunctionAt(PrintWriter out, Listing listing, FunctionManager fm,
            Address target, Address winStart, Address winEnd, ProgramContext ctx, Register tmodeReg, boolean thumb) throws Exception {
        clearListing(winStart, winEnd);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, winStart, winEnd, thumb ? BigInteger.ONE : BigInteger.ZERO);
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
            while (ins.hasNext() && count < 400) {
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
            while (ins.hasNext() && printed < 600) {
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
        out.println();
    }

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/ecomserver_entry.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // Entry point per ROM header: code_address = entry_point = 0x804C7DC8.
        dumpFunctionAt(out, listing, fm, toAddr(0x804C7DC8L), toAddr(0x804C7DC8L), toAddr(0x804C7F00L), ctx, tmodeReg, true);

        // 0x804C7DC8 is a thunk straight into the real main via "blx 0x804cd43c" -
        // an EVEN target address, meaning BLX switches to ARM mode here, not Thumb.
        dumpFunctionAt(out, listing, fm, toAddr(0x804CD43CL), toAddr(0x804CD43CL), toAddr(0x804CD700L), ctx, tmodeReg, false);

        out.close();
        println("WROTE " + outPath);
    }
}
