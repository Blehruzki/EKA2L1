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

// Round 37: round 36 found CdlServer never retries session_create, just hammers
// the same dead session forever. Need to know whether the guest code around the
// ecom.dll call (FUN_83485e24, its wrapper 0x83485fde, and the array-accessor
// 0x83486654) ever checks the TRAP'd error/leave code at all, or takes the same
// path unconditionally regardless of what ecom.dll returns.
public class DumpCdlserverErrorCheck extends GhidraScript {
    private void dumpFunctionAt(PrintWriter out, Listing listing, FunctionManager fm,
            Address target, Address winStart, Address winEnd, ProgramContext ctx, Register tmodeReg) throws Exception {
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
                // ignore - fall back to raw window dump below
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
        out.println();
    }

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_error_check.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // FUN_83485e24 - builds linked-list node, calls into ecom.dll (round 31/34).
        // Widened to include round 31's own merge-routine entry (0x83485d62), since
        // 0x83485e24's branches loop back to addresses before it.
        dumpFunctionAt(out, listing, fm, toAddr(0x83485e24L), toAddr(0x83485d60L), toAddr(0x83485fe0L), ctx, tmodeReg);

        // The small wrapper at 0x83485fde that calls FUN_83485e24 (round 29/34).
        dumpFunctionAt(out, listing, fm, toAddr(0x83485fdeL), toAddr(0x83485fc0L), toAddr(0x83486100L), ctx, tmodeReg);

        // FUN_83486654 - array-element accessor (round 32), also in this call chain.
        dumpFunctionAt(out, listing, fm, toAddr(0x83486654L), toAddr(0x83486600L), toAddr(0x83486700L), ctx, tmodeReg);

        out.close();
        println("WROTE " + outPath);
    }
}
