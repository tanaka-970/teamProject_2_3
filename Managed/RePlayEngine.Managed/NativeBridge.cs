using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace ReplayEngine;

public static unsafe class NativeBridge
{
    private static readonly ScriptRuntimeContext Context = new();
    private static readonly Dictionary<ulong, ManagedInstance> Instances = new();
    private static readonly Dictionary<TypeGuid, Type> Types = new();
    private static AssemblyLoadContext? scriptContext;
    private static Assembly? scriptAssembly;
    private static ulong nextHandle = 1;
    private static string lastError = string.Empty;
    private static string lastErrorFile = string.Empty;
    private static int lastErrorLine;

    private static NativeApi api;

    internal static float TimeDeltaTime { get; private set; }
    internal static float TimeFixedDeltaTime { get; private set; }
    internal static ulong TimeFrameIndex { get; private set; }

    [StructLayout(LayoutKind.Sequential)]
    private struct TypeGuid : IEquatable<TypeGuid>
    {
        public ulong High;
        public ulong Low;

        public bool Equals(TypeGuid other) => High == other.High && Low == other.Low;
        public override bool Equals(object? obj) => obj is TypeGuid other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(High, Low);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NativeApi
    {
        public delegate* unmanaged[Cdecl]<ulong, ObjectHandle*, int> FindGameObject;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int> IsGameObjectValid;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalPosition;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalRotationEuler;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalRotationEuler;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3*, int> GetLocalScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, Vector3, int> SetLocalScale;
        public delegate* unmanaged[Cdecl]<ObjectHandle, uint, ComponentHandle*, int> GetComponent;
        public delegate* unmanaged[Cdecl]<ObjectHandle, int> DestroyGameObject;
        public delegate* unmanaged[Cdecl]<ComponentHandle, int> DestroyComponent;
        public delegate* unmanaged[Cdecl]<byte*, Vector3, Vector3, Vector3, ObjectHandle, ObjectHandle*, int> Instantiate;
        public delegate* unmanaged[Cdecl]<byte*, int> LoadScene;
        public delegate* unmanaged[Cdecl]<int> ReloadScene;
        public delegate* unmanaged[Cdecl]<int> ReturnToPreviousScene;
    }

    private sealed class ManagedInstance
    {
        public ManagedInstance(ScriptBehaviour behaviour)
        {
            Behaviour = behaviour;
        }

        public ScriptBehaviour Behaviour { get; }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetNativeApi(NativeApi* table)
    {
        if (table == null)
        {
            api = default;
            return 0;
        }

        api = *table;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadAssembly(byte* assemblyPath, byte* output, int outputCapacity)
    {
        try
        {
            var path = FromUtf8(assemblyPath);
            if (string.IsNullOrWhiteSpace(path))
            {
                return Fail("C# assembly path is empty.", output, outputCapacity);
            }

            if (Instances.Count != 0)
            {
                return Fail("Managed instances are still alive. Stop Play or destroy instances before Assembly Reload.", output, outputCapacity);
            }

            UnloadScriptContext();
            Types.Clear();

            var context = new AssemblyLoadContext("RePlayEngine.CSharpScripts", isCollectible: true);
            var assembly = context.LoadFromAssemblyPath(path);
            foreach (var type in DiscoverBehaviourTypes(assembly))
            {
                var guid = ReadGuid(type);
                if (guid.HasValue)
                {
                    Types[guid.Value] = type;
                }
            }

            scriptContext = context;
            scriptAssembly = assembly;
            WriteUtf8($"Loaded {Types.Count} C# Behaviour type(s).", output, outputCapacity);
            ClearLastError();
            return 1;
        }
        catch (Exception ex)
        {
            return Fail(ex, output, outputCapacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int UnloadAssembly(byte* output, int outputCapacity)
    {
        if (Instances.Count != 0)
        {
            return Fail("Managed instances are still alive.", output, outputCapacity);
        }

        UnloadScriptContext();
        Types.Clear();
        WriteUtf8("Unloaded C# assembly.", output, outputCapacity);
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DescribeType(ulong high, ulong low, byte* output, int outputCapacity)
    {
        try
        {
            var guid = new TypeGuid { High = high, Low = low };
            if (!Types.TryGetValue(guid, out var type))
            {
                return Fail("C# Behaviour type is not loaded.", output, outputCapacity);
            }

            var schema = BuildSchema(type);
            WriteUtf8(schema, output, outputCapacity);
            return 1;
        }
        catch (Exception ex)
        {
            return Fail(ex, output, outputCapacity);
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static ulong CreateInstance(ulong high, ulong low, ObjectHandle owner, ComponentHandle component)
    {
        try
        {
            var guid = new TypeGuid { High = high, Low = low };
            if (!Types.TryGetValue(guid, out var type))
            {
                SetLastError("C# Behaviour type is not loaded.");
                return 0;
            }

            if (Activator.CreateInstance(type) is not ScriptBehaviour behaviour)
            {
                SetLastError("C# Behaviour could not be created.");
                return 0;
            }

            behaviour.Attach(Context, owner, component);
            var handle = nextHandle++;
            Instances[handle] = new ManagedInstance(behaviour);
            ClearLastError();
            return handle;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyInstance(ulong instance)
    {
        return Instances.Remove(instance) ? 1 : 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Invoke(ulong instance, int callback, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 2;

            switch (callback)
            {
                case 0: state.Behaviour.Awake(); break;
                case 1: state.Behaviour.OnEnable(); break;
                case 2: state.Behaviour.Start(); break;
                case 3: state.Behaviour.FixedUpdate(deltaTime); break;
                case 4: state.Behaviour.Update(deltaTime); break;
                case 5: state.Behaviour.LateUpdate(deltaTime); break;
                case 6: state.Behaviour.OnDisable(); break;
                case 7: state.Behaviour.OnDestroy(); break;
                default: return 1;
            }

            ClearLastError();
            return 0;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 4;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetField(ulong instance, byte* savedName, int valueType, byte* valueText)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 0;
            var fieldName = StripFieldPrefix(FromUtf8(savedName));
            var field = FindSerializableField(state.Behaviour.GetType(), fieldName);
            if (field == null) return 0;

            var parsed = ParseValue(field.FieldType, FromUtf8(valueText));
            field.SetValue(state.Behaviour, parsed);
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetField(ulong instance, byte* savedName, byte* output, int outputCapacity)
    {
        try
        {
            if (!Instances.TryGetValue(instance, out var state)) return 0;
            var fieldName = StripFieldPrefix(FromUtf8(savedName));
            var field = FindSerializableField(state.Behaviour.GetType(), fieldName);
            if (field == null) return 0;

            WriteUtf8(FormatValue(field.GetValue(state.Behaviour), field.FieldType), output, outputCapacity);
            return 1;
        }
        catch (Exception ex)
        {
            SetLastError(ex);
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetTime(float deltaTime, float fixedDeltaTime, ulong frameIndex)
    {
        TimeDeltaTime = deltaTime;
        TimeFixedDeltaTime = fixedDeltaTime;
        TimeFrameIndex = frameIndex;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LiveInstanceCount()
    {
        return Instances.Count;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LastError(byte* message, int messageCapacity, byte* file, int fileCapacity, int* line)
    {
        WriteUtf8(lastError, message, messageCapacity);
        WriteUtf8(lastErrorFile, file, fileCapacity);
        if (line != null) *line = lastErrorLine;
        return string.IsNullOrEmpty(lastError) ? 0 : 1;
    }

    internal static RuntimeResult<ObjectHandle> FindGameObject(ulong objectId)
    {
        if (api.FindGameObject == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle result = default;
        var status = (RuntimeStatus)api.FindGameObject(objectId, &result);
        return new RuntimeResult<ObjectHandle>(status, result);
    }

    internal static RuntimeStatus IsGameObjectValid(ObjectHandle handle)
    {
        if (api.IsGameObjectValid == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.IsGameObjectValid(handle);
    }

    internal static RuntimeResult<Vector3> GetLocalPosition(ObjectHandle handle)
    {
        if (api.GetLocalPosition == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalPosition(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalPosition(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalPosition == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalPosition(handle, value);
    }

    internal static RuntimeResult<Vector3> GetLocalRotationEuler(ObjectHandle handle)
    {
        if (api.GetLocalRotationEuler == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalRotationEuler(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalRotationEuler(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalRotationEuler == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalRotationEuler(handle, value);
    }

    internal static RuntimeResult<Vector3> GetLocalScale(ObjectHandle handle)
    {
        if (api.GetLocalScale == null) return new(RuntimeStatus.ServiceUnavailable);
        Vector3 value = default;
        return new RuntimeResult<Vector3>((RuntimeStatus)api.GetLocalScale(handle, &value), value);
    }

    internal static RuntimeStatus SetLocalScale(ObjectHandle handle, Vector3 value)
    {
        if (api.SetLocalScale == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.SetLocalScale(handle, value);
    }

    internal static RuntimeResult<ComponentHandle> GetComponent(ObjectHandle handle, uint componentTypeId)
    {
        if (api.GetComponent == null) return new(RuntimeStatus.ServiceUnavailable);
        ComponentHandle value = default;
        return new RuntimeResult<ComponentHandle>((RuntimeStatus)api.GetComponent(handle, componentTypeId, &value), value);
    }

    internal static RuntimeStatus DestroyGameObject(ObjectHandle handle)
    {
        if (api.DestroyGameObject == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.DestroyGameObject(handle);
    }

    internal static RuntimeStatus DestroyComponent(ComponentHandle handle)
    {
        if (api.DestroyComponent == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.DestroyComponent(handle);
    }

    internal static RuntimeResult<ObjectHandle> Instantiate(string guid, Vector3 position, Vector3 rotationEuler, Vector3 scale, ObjectHandle parent)
    {
        if (api.Instantiate == null) return new(RuntimeStatus.ServiceUnavailable);
        ObjectHandle value = default;
        fixed (byte* guidUtf8 = Encoding.UTF8.GetBytes(guid + "\0"))
        {
            return new RuntimeResult<ObjectHandle>(
                (RuntimeStatus)api.Instantiate(guidUtf8, position, rotationEuler, scale, parent, &value),
                value);
        }
    }

    internal static RuntimeStatus LoadScene(string sceneAssetGuid)
    {
        if (api.LoadScene == null) return RuntimeStatus.ServiceUnavailable;
        fixed (byte* guidUtf8 = Encoding.UTF8.GetBytes(sceneAssetGuid + "\0"))
        {
            return (RuntimeStatus)api.LoadScene(guidUtf8);
        }
    }

    internal static RuntimeStatus ReloadScene()
    {
        if (api.ReloadScene == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.ReloadScene();
    }

    internal static RuntimeStatus ReturnToPreviousScene()
    {
        if (api.ReturnToPreviousScene == null) return RuntimeStatus.ServiceUnavailable;
        return (RuntimeStatus)api.ReturnToPreviousScene();
    }

    private static IEnumerable<Type> DiscoverBehaviourTypes(Assembly assembly)
    {
        return assembly.GetTypes().Where(type =>
            !type.IsAbstract &&
            typeof(ScriptBehaviour).IsAssignableFrom(type) &&
            type.GetCustomAttribute<ReplayGuidAttribute>() != null);
    }

    private static TypeGuid? ReadGuid(Type type)
    {
        var text = type.GetCustomAttribute<ReplayGuidAttribute>()?.Value;
        if (string.IsNullOrWhiteSpace(text)) return null;

        Span<char> digits = stackalloc char[32];
        var count = 0;
        foreach (var c in text)
        {
            if (c == '-' || c == '{' || c == '}') continue;
            if (!Uri.IsHexDigit(c) || count >= digits.Length) return null;
            digits[count++] = char.ToLowerInvariant(c);
        }

        if (count != 32) return null;
        var high = ulong.Parse(new string(digits[..16]), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        var low = ulong.Parse(new string(digits[16..]), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        return new TypeGuid { High = high, Low = low };
    }

    private static string BuildSchema(Type type)
    {
        var builder = new StringBuilder();
        foreach (var field in SerializableFields(type))
        {
            var mapped = MapFieldType(field.FieldType);
            if (mapped == null) continue;

            var defaultValue = DefaultValue(type, field);
            builder.Append("FIELD\t");
            builder.Append(Escape(field.Name));
            builder.Append('\t');
            builder.Append(mapped);
            builder.Append('\t');
            builder.Append(Escape(Humanize(field.Name)));
            builder.Append('\t');
            builder.Append(Escape(string.Empty));
            builder.Append('\t');
            builder.Append(Escape(FormatValue(defaultValue, field.FieldType)));
            builder.Append('\n');
        }

        return builder.ToString();
    }

    private static IEnumerable<FieldInfo> SerializableFields(Type type)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        return type.GetFields(flags)
            .Where(field =>
                !field.IsStatic &&
                !field.IsInitOnly &&
                !field.IsDefined(typeof(NonSerializedAttribute), true) &&
                (field.IsPublic || field.IsDefined(typeof(SerializeFieldAttribute), true)))
            .OrderBy(field => field.MetadataToken);
    }

    private static FieldInfo? FindSerializableField(Type type, string fieldName)
    {
        return SerializableFields(type).FirstOrDefault(field => field.Name == fieldName);
    }

    private static object? DefaultValue(Type type, FieldInfo field)
    {
        try
        {
            var instance = Activator.CreateInstance(type);
            return field.GetValue(instance);
        }
        catch
        {
            return field.FieldType.IsValueType ? Activator.CreateInstance(field.FieldType) : null;
        }
    }

    private static string? MapFieldType(Type type)
    {
        if (type == typeof(bool)) return "bool";
        if (type == typeof(int)) return "int";
        if (type == typeof(long)) return "int64";
        if (type == typeof(ulong)) return "uint64";
        if (type == typeof(float)) return "float";
        if (type == typeof(double)) return "double";
        if (type == typeof(string)) return "string";
        if (type == typeof(Vector2)) return "vector2";
        if (type == typeof(Vector3)) return "vector3";
        if (type == typeof(Vector4)) return "vector4";
        if (type == typeof(Quaternion)) return "quaternion";
        if (type == typeof(Color)) return "color";
        if (type == typeof(ObjectReference)) return "object";
        if (type == typeof(ComponentReference)) return "component";
        return null;
    }

    private static object? ParseValue(Type type, string text)
    {
        var parts = text.Split(',');
        if (type == typeof(bool)) return text == "1" || text.Equals("true", StringComparison.OrdinalIgnoreCase);
        if (type == typeof(int)) return int.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(long)) return long.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(ulong)) return ulong.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(float)) return float.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(double)) return double.Parse(text, CultureInfo.InvariantCulture);
        if (type == typeof(string)) return text;
        if (type == typeof(Vector2)) return new Vector2(ParseFloat(parts, 0), ParseFloat(parts, 1));
        if (type == typeof(Vector3)) return new Vector3(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2));
        if (type == typeof(Vector4)) return new Vector4(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(Quaternion)) return new Quaternion(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(Color)) return new Color(ParseFloat(parts, 0), ParseFloat(parts, 1), ParseFloat(parts, 2), ParseFloat(parts, 3));
        if (type == typeof(ObjectReference)) return new ObjectReference { ObjectId = ParseULong(parts, 0) };
        if (type == typeof(ComponentReference)) return new ComponentReference { OwnerObjectId = ParseULong(parts, 0), ComponentStableId = (uint)ParseULong(parts, 1) };
        return type.IsValueType ? Activator.CreateInstance(type) : null;
    }

    private static string FormatValue(object? value, Type type)
    {
        if (value == null) return string.Empty;
        if (type == typeof(bool)) return ((bool)value) ? "true" : "false";
        if (type == typeof(int)) return ((int)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(long)) return ((long)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(ulong)) return ((ulong)value).ToString(CultureInfo.InvariantCulture);
        if (type == typeof(float)) return ((float)value).ToString("R", CultureInfo.InvariantCulture);
        if (type == typeof(double)) return ((double)value).ToString("R", CultureInfo.InvariantCulture);
        if (type == typeof(string)) return (string)value;
        if (type == typeof(Vector2))
        {
            var v = (Vector2)value;
            return $"{F(v.X)},{F(v.Y)}";
        }
        if (type == typeof(Vector3))
        {
            var v = (Vector3)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)}";
        }
        if (type == typeof(Vector4))
        {
            var v = (Vector4)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)},{F(v.W)}";
        }
        if (type == typeof(Quaternion))
        {
            var v = (Quaternion)value;
            return $"{F(v.X)},{F(v.Y)},{F(v.Z)},{F(v.W)}";
        }
        if (type == typeof(Color))
        {
            var v = (Color)value;
            return $"{F(v.R)},{F(v.G)},{F(v.B)},{F(v.A)}";
        }
        if (type == typeof(ObjectReference)) return ((ObjectReference)value).ObjectId.ToString(CultureInfo.InvariantCulture);
        if (type == typeof(ComponentReference))
        {
            var reference = (ComponentReference)value;
            return $"{reference.OwnerObjectId.ToString(CultureInfo.InvariantCulture)},{reference.ComponentStableId.ToString(CultureInfo.InvariantCulture)}";
        }

        return string.Empty;
    }

    private static string F(float value) => value.ToString("R", CultureInfo.InvariantCulture);
    private static float ParseFloat(string[] parts, int index) => index < parts.Length ? float.Parse(parts[index], CultureInfo.InvariantCulture) : 0.0f;
    private static ulong ParseULong(string[] parts, int index) => index < parts.Length ? ulong.Parse(parts[index], CultureInfo.InvariantCulture) : 0UL;

    private static string Humanize(string name)
    {
        if (string.IsNullOrEmpty(name)) return name;

        var builder = new StringBuilder();
        for (var i = 0; i < name.Length; ++i)
        {
            var c = name[i];
            if (i > 0 && char.IsUpper(c) && (char.IsLower(name[i - 1]) || char.IsDigit(name[i - 1])))
            {
                builder.Append(' ');
            }
            else if (c == '_' || c == '-')
            {
                builder.Append(' ');
                continue;
            }

            builder.Append(builder.Length == 0 ? char.ToUpperInvariant(c) : c);
        }

        return builder.ToString();
    }

    private static void UnloadScriptContext()
    {
        scriptAssembly = null;
        if (scriptContext == null) return;
        scriptContext.Unload();
        scriptContext = null;
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static int Fail(Exception ex, byte* output, int outputCapacity)
    {
        SetLastError(ex);
        WriteUtf8(lastError, output, outputCapacity);
        return 0;
    }

    private static int Fail(string message, byte* output, int outputCapacity)
    {
        SetLastError(message);
        WriteUtf8(message, output, outputCapacity);
        return 0;
    }

    private static void SetLastError(Exception ex)
    {
        lastError = ex.Message;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static void SetLastError(string message)
    {
        lastError = message;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static void ClearLastError()
    {
        lastError = string.Empty;
        lastErrorFile = string.Empty;
        lastErrorLine = 0;
    }

    private static string StripFieldPrefix(string savedName)
    {
        const string prefix = "field.";
        return savedName.StartsWith(prefix, StringComparison.Ordinal) ? savedName[prefix.Length..] : savedName;
    }

    private static string FromUtf8(byte* text)
    {
        if (text == null) return string.Empty;
        var length = 0;
        while (text[length] != 0) ++length;
        return Encoding.UTF8.GetString(text, length);
    }

    private static void WriteUtf8(string text, byte* output, int capacity)
    {
        if (output == null || capacity <= 0) return;
        var bytes = Encoding.UTF8.GetBytes(text);
        var count = Math.Min(bytes.Length, capacity - 1);
        for (var i = 0; i < count; ++i) output[i] = bytes[i];
        output[count] = 0;
    }

    private static string Escape(string text)
    {
        var builder = new StringBuilder(text.Length);
        foreach (var c in text)
        {
            if (c == '%' || c == '\t' || c == '\r' || c == '\n')
            {
                builder.Append('%');
                builder.Append(((int)c).ToString("X2", CultureInfo.InvariantCulture));
            }
            else
            {
                builder.Append(c);
            }
        }

        return builder.ToString();
    }
}
