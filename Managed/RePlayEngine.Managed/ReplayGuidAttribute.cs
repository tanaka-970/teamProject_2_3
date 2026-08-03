using System;

namespace ReplayEngine;

[AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
public sealed class ReplayGuidAttribute : Attribute
{
    public ReplayGuidAttribute(string value)
    {
        Value = value;
    }

    public string Value { get; }
}

[AttributeUsage(AttributeTargets.Field, Inherited = true, AllowMultiple = false)]
public sealed class SerializeFieldAttribute : Attribute
{
}
