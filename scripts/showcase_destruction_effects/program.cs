// Original showcase effect parameters; binary serialization is Effekseer's official exporter.
using System;
using System.IO;
using Effekseer;
using Effekseer.Data;
using Effekseer.Data.Value;
using Path = System.IO.Path;

internal static class Program
{
    private static void Range(FloatWithRandom value, float center, float amplitude = 0)
    {
        value.SetCenter(center);
        value.SetAmplitude(amplitude);
    }

    private static void Vector(Vector3DWithRandom value, float x, float y, float z,
                               float ax = 0, float ay = 0, float az = 0)
    {
        Range(value.X, x, ax);
        Range(value.Y, y, ay);
        Range(value.Z, z, az);
    }

    private static Node Sprite(string name, string texture, int count, int life,
                               int r, int g, int b, int alpha, bool additive)
    {
        var node = Core.Root.AddChild();
        node.Name.SetValue(name);
        node.CommonValues.MaxGeneration.Value.SetValue(count);
        node.CommonValues.Life.SetCenter(life);
        node.CommonValues.Life.SetAmplitude(Math.Max(1, life / 8));
        Range(node.CommonValues.Generation.GenerationTime, 0.12f);
        node.DrawingValues.Type.SetValue(RendererValues.ParamaterType.Sprite);
        node.DrawingValues.ColorAll.Fixed.R.SetValue(r);
        node.DrawingValues.ColorAll.Fixed.G.SetValue(g);
        node.DrawingValues.ColorAll.Fixed.B.SetValue(b);
        node.DrawingValues.ColorAll.Fixed.A.SetValue(alpha);
        node.RendererCommonValues.ColorTexture.SetAbsolutePath(texture);
        node.RendererCommonValues.AlphaBlend.SetValue(additive ? AlphaBlendType.Add : AlphaBlendType.Blend);
        node.RendererCommonValues.ZTest.SetValue(true);
        node.RendererCommonValues.ZWrite.SetValue(false);
        node.RendererCommonValues.FadeInType.SetValue(RendererCommonValues.FadeInMethod.Use);
        node.RendererCommonValues.FadeIn.Frame.SetValue(2);
        node.RendererCommonValues.FadeOutType.SetValue(RendererCommonValues.FadeOutMethod.WithinLifetime);
        node.RendererCommonValues.FadeOut.Frame.SetValue(life * 0.6f);
        node.LocationValues.Type.SetValue(LocationValues.ParamaterType.PVA);
        node.ScalingValues.Type.SetValue(ScaleValues.ParamaterType.SinglePVA);
        return node;
    }

    private static void Dust(string smoke, bool burst)
    {
        var node = Sprite("Rising earth dust", smoke, burst ? 28 : 22, burst ? 95 : 145,
                          153, 137, 111, 185, false);
        Vector(node.LocationValues.PVA.Location, 0, 1.0f, 0, 1.6f, 0.35f, 1.6f);
        Vector(node.LocationValues.PVA.Velocity, 0, 0.045f, 0, 0.04f, 0.02f, 0.04f);
        Range(node.ScalingValues.SinglePVA.Scale, 2.6f, 0.55f);
        Range(node.ScalingValues.SinglePVA.Velocity, 0.036f, 0.01f);
        // Smoke.png is a 512-square 2x2 atlas. Effekseer UV coordinates are pixels.
        node.RendererCommonValues.UV.SetValue(RendererCommonValues.UVType.Fixed);
        node.RendererCommonValues.UVFixed.Size.X.SetValue(256);
        node.RendererCommonValues.UVFixed.Size.Y.SetValue(256);
    }

    private static void MakeEffect(string path, string smoke, string particle, bool burst)
    {
        Core.New();
        Core.Root.ClearChildren();
        Core.Root.SetFullPath(Path.GetFullPath(path));
        Core.EndFrame = 150;
        Core.IsLoop = false;
        Dust(smoke, burst);
        if (burst)
        {
            var sparks = Sprite("Hot fragments", particle, 100, 42, 255, 158, 58, 255, true);
            Vector(sparks.LocationValues.PVA.Velocity, 0, 0.18f, 0, 0.32f, 0.18f, 0.32f);
            Vector(sparks.LocationValues.PVA.Acceleration, 0, -0.003f, 0);
            Range(sparks.ScalingValues.SinglePVA.Scale, 0.28f, 0.1f);
            var flash = Sprite("Blast flash", particle, 1, 12, 255, 210, 125, 255, true);
            Vector(flash.LocationValues.PVA.Location, 0, 0.5f, 0);
            Range(flash.ScalingValues.SinglePVA.Scale, 4.0f);
            Range(flash.ScalingValues.SinglePVA.Velocity, 0.18f);
            flash.RendererCommonValues.EmissiveScaling.SetValue(3.5f);
            var fire = Sprite("Expanding fireball", particle, 14, 24, 255, 95, 22, 210, true);
            Vector(fire.LocationValues.PVA.Location, 0, 0.5f, 0, 0.65f, 0.4f, 0.65f);
            Vector(fire.LocationValues.PVA.Velocity, 0, 0.04f, 0, 0.08f, 0.07f, 0.08f);
            Range(fire.ScalingValues.SinglePVA.Scale, 1.8f, 0.6f);
            Range(fire.ScalingValues.SinglePVA.Velocity, 0.07f);
            fire.RendererCommonValues.EmissiveScaling.SetValue(2.5f);
        }
        Core.SaveTo(path);
        Console.WriteLine($"Exported {Path.GetFileName(path)} with official Effekseer {Core.Version}");
    }

    private static void MakeTrail(string path, string smoke, string particle)
    {
        Core.New();
        Core.Root.ClearChildren();
        Core.Root.SetFullPath(Path.GetFullPath(path));
        Core.EndFrame = 300;
        Core.IsLoop = false;
        var exhaust = Sprite("Rocket exhaust", particle, 1, 16, 255, 165, 52, 245, true);
        exhaust.CommonValues.MaxGeneration.Infinite.SetValue(true);
        Range(exhaust.CommonValues.Generation.GenerationTime, 1.0f);
        exhaust.CommonValues.LocationEffectType.SetValue(TranslationParentEffectType.WhenCreating);
        exhaust.CommonValues.RotationEffectType.SetValue(ParentEffectType.WhenCreating);
        Vector(exhaust.LocationValues.PVA.Location, 0, 0, 0.6f, 0.04f, 0.04f, 0);
        Vector(exhaust.LocationValues.PVA.Velocity, 0, 0, 0.08f);
        Range(exhaust.ScalingValues.SinglePVA.Scale, 0.5f, 0.12f);
        exhaust.RendererCommonValues.EmissiveScaling.SetValue(4.0f);
        var plume = Sprite("Rocket smoke trail", smoke, 1, 45, 165, 153, 130, 135, false);
        plume.CommonValues.MaxGeneration.Infinite.SetValue(true);
        Range(plume.CommonValues.Generation.GenerationTime, 2.0f);
        plume.CommonValues.LocationEffectType.SetValue(TranslationParentEffectType.WhenCreating);
        plume.CommonValues.RotationEffectType.SetValue(ParentEffectType.WhenCreating);
        Vector(plume.LocationValues.PVA.Location, 0, 0, 0.7f);
        Range(plume.ScalingValues.SinglePVA.Scale, 0.42f, 0.1f);
        Range(plume.ScalingValues.SinglePVA.Velocity, 0.015f);
        plume.RendererCommonValues.UV.SetValue(RendererCommonValues.UVType.Fixed);
        plume.RendererCommonValues.UVFixed.Size.X.SetValue(256);
        plume.RendererCommonValues.UVFixed.Size.Y.SetValue(256);
        Core.SaveTo(path);
        Console.WriteLine("Exported rocket-trail.efkefc with official Effekseer exporter");
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.Error.WriteLine("Usage: showcase_effects <repository-root>");
            return 1;
        }
        var root = Path.GetFullPath(args[0]);
        var target = Path.Combine(root, "projects/showcase/assets/effects/destruction");
        var textures = Path.Combine(target, "textures");
        Directory.CreateDirectory(textures);
        var upstream = Path.Combine(root, "third_party/efkbgfx/examples/resources/Texture");
        var smoke = Path.Combine(textures, "smoke.png");
        var particle = Path.Combine(textures, "particle-soft.png");
        File.Copy(Path.Combine(upstream, "Smoke.png"), smoke, true);
        File.Copy(Path.Combine(upstream, "Particle_Soft.png"), particle, true);
        Core.Initialize();
        MakeEffect(Path.Combine(target, "dust.efkefc"), smoke, particle, false);
        MakeEffect(Path.Combine(target, "burst.efkefc"), smoke, particle, true);
        MakeTrail(Path.Combine(target, "rocket-trail.efkefc"), smoke, particle);
        return 0;
    }
}
