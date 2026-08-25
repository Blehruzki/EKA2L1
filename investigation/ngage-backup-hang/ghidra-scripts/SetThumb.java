import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;

public class SetThumb extends GhidraScript {
    @Override
    protected void run() throws Exception {
        ProgramContext ctx = currentProgram.getProgramContext();
        Register reg = ctx.getRegister("TMode");
        if (reg == null) {
            println("WARNING: no TMode register found on this language");
            return;
        }
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            ctx.setValue(reg, block.getStart(), block.getEnd(), java.math.BigInteger.ONE);
        }
        println("TMode set to 1 across all memory blocks");
    }
}
