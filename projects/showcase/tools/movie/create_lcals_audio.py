import wave, struct, math, random, sys
sr=48000; duration=5; random.seed(701)
# Original synthesized sound: rising air, soft low bloom and a gentle three-note shimmer.
phases=[0.0,0.0,0.0]
freqs=[261.625565,329.627557,391.995436]
with wave.open(sys.argv[1], 'wb') as out:
    out.setnchannels(2); out.setsampwidth(2); out.setframerate(sr)
    block=bytearray()
    for i in range(sr*duration):
        t=i/sr
        def smooth(x):
            x=max(0.0,min(1.0,x)); return x*x*(3-2*x)
        env=(smooth((t-0.18)/0.9)*(1-smooth((t-1.45)/0.65)))
        noise=(random.random()*2-1)
        sweep=0.014*env*(math.sin(2*math.pi*(160*t+190*t*t))+0.5*noise)
        impact=max(0,t-1.22)
        bloom=0.16*math.exp(-impact*3.3)*math.sin(2*math.pi*(72*impact-7*impact*impact)) if t>=1.22 else 0
        bells=0.0
        for j,f in enumerate(freqs):
            d=t-(1.28+j*0.12)
            if d>=0:
                e=(1-math.exp(-d*18))*math.exp(-d*1.25)
                bells += 0.095*e*(math.sin(2*math.pi*f*d)+0.25*math.sin(2*math.pi*f*2*d)*math.exp(-d*2))
        fade=1-smooth((t-3.95)/0.9)
        left=max(-1,min(1,(sweep+bloom+bells)*fade))
        right=max(-1,min(1,(sweep*0.9+bloom+bells*0.95)*fade))
        block.extend(struct.pack('<hh',int(left*32767),int(right*32767)))
        if len(block)>=65536:
            out.writeframesraw(block);block.clear()
    out.writeframes(block)
