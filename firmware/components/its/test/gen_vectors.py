#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate test/vectors.h for the ITS decoder host tests.

Run with:   python3 -I gen_vectors.py            (writes vectors.h next to this file)
Requires:   pip install asn1tools

The messages are encoded with asn1tools from the ETSI ASN.1 modules in asn1/
(CAM, DENM) and from asn1/dsrc-subset.asn (MAPEM / SPATEM), wrapped in complete
802.11 / LLC-SNAP / GeoNetworking / BTP frames (unsecured and inside an
IEEE 1609.2 signedData envelope built by hand per the COER rules) and emitted
as C byte arrays together with the expected decoding results. The expected
values are derived from the *input* values of the encoder, never from our
decoder. The output is deterministic (fixed seeds), so vectors.h is checked in.
"""
import math
import os
import random
import re
import sys

import asn1tools

HERE = os.path.dirname(os.path.abspath(__file__))
A = os.path.join(HERE, "asn1") + os.sep


def compile_asn(files, text_patch=None):
    if text_patch is None:
        return asn1tools.compile_files([A + f for f in files], "uper")
    srcs = {}
    for f in files:
        with open(A + f) as fh:
            srcs[f] = text_patch(f, fh.read())
    return asn1tools.compile_string("\n".join(srcs.values()), "uper")


# ---------------------------------------------------------------------------
# ASN.1 codecs
# ---------------------------------------------------------------------------
V1 = compile_asn(["TS102894-2v131-CDD.asn", "EN302637-2v141-CAM.asn", "EN302637-3v131-DENM.asn"])
V2 = compile_asn(["release2/TS102894-2v241-CDD.asn", "release2/TS103900v231-CAM.asn",
                  "release2/TS103831v231-DENM.asn"])
DSRC = compile_asn(["dsrc-subset.asn"])


def future_dsrc_text(_f, text):
    """Add extension additions / extension alternatives to many types ("a later
    edition of the standard"), so the decoder's skipping logic is exercised."""
    def add_seq(t, additions):
        nonlocal text
        pat = re.compile(r"(\n%s ::= SEQUENCE \{.*?\n    \.\.\.)(\n\})" % re.escape(t), re.S)
        text, n = pat.subn(lambda m: m.group(1) + ",\n    " + additions + m.group(2), text, count=1)
        assert n == 1, t

    ft = "futA INTEGER (0..100000) OPTIONAL, futB BOOLEAN OPTIONAL, futC IA5String (SIZE(1..9)) OPTIONAL"
    for t in ["MapData", "IntersectionGeometry", "GenericLane", "NodeXY", "Position3D", "SPAT",
              "IntersectionState", "MovementState", "MovementEvent", "NodeAttributeSetXY",
              "ConnectionManeuverAssist", "AdvisorySpeed", "ComputedLane", "RoadSegment"]:
        add_seq(t, ft)
    # CHOICE extension alternatives
    text = text.replace("    parking LaneAttributes-Parking,\n    ...",
                        "    parking LaneAttributes-Parking,\n    ...,\n    futureLane BIT STRING (SIZE(5..40))")
    text = text.replace("    computed ComputedLane,\n    ...",
                        "    computed ComputedLane,\n    ...,\n    futureNodes OCTET STRING (SIZE(0..9))")
    text = text.replace("    regional RegionalExtensionList,\n    ...\n}\n\nComputedLane",
                        "    regional RegionalExtensionList,\n    ...,\n    futureAngle INTEGER (0..1000)\n}\n\nComputedLane")
    # extensible ENUMERATED values
    text = text.replace("hydrantPresent, ... }", "hydrantPresent, ..., futureAttr1, futureAttr2 }")
    text = text.replace("unEvenPavementPresent, ... }", "unEvenPavementPresent, ..., futureSeg }")
    text = text.replace("vehiclesWithTrailersNightMaxSpeed, ... }",
                        "vehiclesWithTrailersNightMaxSpeed, ..., futureLimit }")
    text = text.replace("sharedLaneData, ... }", "sharedLaneData, ..., futureLayer }")
    return text


DSRCF = compile_asn(["dsrc-subset.asn"], future_dsrc_text)

# CAM / DENM with extension additions in BasicContainer / ManagementContainer
def cam_ext_text(f, text):
    if f.endswith("EN302637-2v141-CAM.asn"):
        text = text.replace("referencePosition ReferencePosition,\n    ...\n}",
                            "referencePosition ReferencePosition,\n    ...,\n    futX INTEGER (0..70000) OPTIONAL,\n    futY BOOLEAN OPTIONAL\n}")
        assert "futX" in text
    if f.endswith("EN302637-3v131-DENM.asn"):
        text = text.replace("stationType StationType,\n    ...\n}",
                            "stationType StationType,\n    ...,\n    futZ IA5String (SIZE(1..5)) OPTIONAL\n}", 1)
        assert "futZ" in text
    return text


V1X = compile_asn(["TS102894-2v131-CDD.asn", "EN302637-2v141-CAM.asn", "EN302637-3v131-DENM.asn"], cam_ext_text)

# ---------------------------------------------------------------------------
# Frame construction
# ---------------------------------------------------------------------------
rng = random.Random(0x1750)
MAC_A = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55])
MAC_B = bytes([0x06, 0xaa, 0xbb, 0xcc, 0xdd, 0xee])
BCAST = b"\xff" * 6
LLC = bytes([0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x89, 0x47])


def wlan(qos=True, src=MAC_A, fc1=0x00):
    fc0 = 0x88 if qos else 0x08
    h = bytes([fc0, fc1]) + b"\x00\x00" + BCAST + src + BCAST + b"\x10\x00"
    if qos:
        h += b"\x00\x00"
    return h


def lpv(st=5, mac=MAC_A, lat=520123456, lon=134567890):
    gn_addr = bytes([0x80 | (st << 2), 0x00]) + mac            # M=1, ST, reserved, MID
    return (gn_addr + (0x12345678).to_bytes(4, "big") + lat.to_bytes(4, "big", signed=True)
            + lon.to_bytes(4, "big", signed=True) + b"\x00\x10" + b"\x03\x84")


def spv():
    return (b"\x80\x00" + MAC_B + (7).to_bytes(4, "big") + (520000000).to_bytes(4, "big", signed=True)
            + (130000000).to_bytes(4, "big", signed=True))[:20]


def gn_inner(pdu, port, ht=5, hst=0, btp="B", st=5, mhl=1):
    """GN common header + extended header + BTP + ITS PDU."""
    btp_hdr = port.to_bytes(2, "big") + (b"\x00\x00" if btp == "B" else (0x1234).to_bytes(2, "big"))
    payload = btp_hdr + pdu
    nh = 2 if btp == "B" else 1
    if ht == 5 and hst == 0:
        ext = lpv(st) + b"\x00\x00\x00\x00"                 # SHB: LPV + media dependent
    elif ht == 5:
        ext = b"\x00\x2a\x00\x00" + lpv(st)                 # TSB (multi hop)
    elif ht == 4 or ht == 3:
        ext = (b"\x00\x2b\x00\x00" + lpv(st) + (520000000).to_bytes(4, "big", signed=True)
               + (130000000).to_bytes(4, "big", signed=True) + (500).to_bytes(2, "big")
               + (500).to_bytes(2, "big") + b"\x00\x00\x00\x00")
    elif ht == 2:
        ext = b"\x00\x2c\x00\x00" + lpv(st) + spv()
    else:
        raise ValueError(ht)
    common = bytes([nh << 4, (ht << 4) | hst, 0x02, 0x80]) + len(payload).to_bytes(2, "big") + bytes([mhl, 0])
    return common + ext + payload, len(common + ext)


def oer_len(n):
    if n < 0x80:
        return bytes([n])
    if n < 0x100:
        return bytes([0x81, n])
    return bytes([0x82]) + n.to_bytes(2, "big")


def secure(inner, variant="signed"):
    """Wrap GN (common header...) in an Ieee1609Dot2Data; returns (bytes, offset of inner)."""
    unsec = b"\x03\x80" + oer_len(len(inner))
    if variant == "unsecured":
        return unsec + inner, len(unsec)
    hdr_info = b"\x40" + b"\x01\x24" + (0x0123456789ABCDEF).to_bytes(8, "big")   # psid + generationTime
    signer = b"\x80" + bytes(rng.randrange(256) for _ in range(8))              # digest
    sig = b"\x80" + b"\x80" + bytes(rng.randrange(256) for _ in range(32)) + bytes(rng.randrange(256) for _ in range(32))
    pre = b"\x03\x81" + b"\x00" + b"\x40"                                       # ver, signedData, hashId, payload preamble
    return pre + unsec + inner + hdr_info + signer + sig, len(pre) + len(unsec)


def build_frame(pdu, port, *, ht=5, hst=0, btp="B", secured=None, qos=True, src=MAC_A, tail=b"",
                st=5, fc1=0x00):
    inner, ext_end = gn_inner(pdu, port, ht, hst, btp, st)
    w = wlan(qos, src, fc1)
    if secured is None:
        gn = bytes([0x11, 0x00, 0x1e, 0x01]) + inner
        pdu_off = len(w) + len(LLC) + 4 + ext_end + 4
        frame = w + LLC + gn + tail
    else:
        body, off = secure(inner, secured)
        gn = bytes([0x12, 0x00, 0x1e, 0x01]) + body
        pdu_off = len(w) + len(LLC) + 4 + off + ext_end + 4
        frame = w + LLC + gn + tail
    return frame, pdu_off


# ---------------------------------------------------------------------------
# Expected-value helpers
# ---------------------------------------------------------------------------
def c_bytes(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    return "static const uint8_t %s[] = {\n%s\n};\n" % (name, "\n".join(lines))


def rev_maneuvers(bits):
    data, n = bits
    v = int.from_bytes(data, "big") >> (len(data) * 8 - n)
    m = 0
    for i in range(min(11, n)):
        if (v >> (n - 1 - i)) & 1:
            m |= 1 << i
    return m


LANE_TYPES = ["vehicle", "crosswalk", "bikeLane", "sidewalk", "median", "striping", "trackedVehicle", "parking"]
XY_BITS = {"node-XY1": 10, "node-XY2": 11, "node-XY3": 12, "node-XY4": 13, "node-XY5": 14, "node-XY6": 16}


def round_away(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def exp_lane(l, ref_lat, ref_lon):
    d = l["laneAttributes"]["directionalUse"][0][0]
    direction = (1 if d & 0x80 else 0) | (2 if d & 0x40 else 0)
    lt = l["laneAttributes"]["laneType"][0]
    ltype = LANE_TYPES.index(lt) if lt in LANE_TYPES else 8
    nodes = []
    x = y = 0
    nl = l["nodeList"]
    if nl[0] == "nodes":
        for n in nl[1]:
            kind, v = n["delta"]
            if kind in XY_BITS:
                x += v["x"]
                y += v["y"]
            elif kind == "node-LatLon":
                cl = math.cos(ref_lat * 1e-7 * math.pi / 180.0)
                x = round_away((v["lon"] - ref_lon) * cl * 1.11195)
                y = round_away((v["lat"] - ref_lat) * 1.11195)
            nodes.append((x, y))
    if len(nodes) > 8:
        n = len(nodes)
        nodes = [nodes[(k * (n - 1) + 3) // 7] for k in range(8)]
    conns = []
    for c in l.get("connectsTo", [])[:4]:
        conns.append((c["connectingLane"]["lane"],
                      rev_maneuvers(c["connectingLane"]["maneuver"]) if "maneuver" in c["connectingLane"] else 0,
                      c.get("signalGroup", 0)))
    return dict(lane_id=l["laneID"], direction=direction, lane_type=ltype,
                ingress=l.get("ingressApproach", 0), egress=l.get("egressApproach", 0),
                maneuvers=rev_maneuvers(l["maneuvers"]) if "maneuvers" in l else 0,
                nodes=nodes, conns=conns)


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_map(varname, inters):
    out = ["static const its_map_intersection_t %s[] = {" % varname]
    for g in inters:
        rp = g["refPoint"]
        ref_lat, ref_lon = rp["lat"], rp["long"]
        lanes = [exp_lane(l, ref_lat, ref_lon) for l in g["laneSet"]][:64]
        out.append("  {")
        out.append("    .region = %d, .id = %d, .revision = %d, .name = %s," % (
            g["id"].get("region", 0), g["id"]["id"], g["revision"], cstr(g.get("name", ""))))
        out.append("    .ref_lat = %d, .ref_lon = %d, .n_lanes = %d," % (ref_lat, ref_lon, len(lanes)))
        out.append("    .lanes = {")
        for L in lanes:
            nx = ", ".join(str(n[0]) for n in L["nodes"]) or "0"
            ny = ", ".join(str(n[1]) for n in L["nodes"]) or "0"
            cs = ", ".join("{ .connecting_lane = %d, .connecting_maneuver = %d, .signal_group = %d }" % c
                           for c in L["conns"]) or "{ 0 }"
            out.append("      { .lane_id = %d, .direction = %d, .lane_type = %d, .ingress_approach = %d, "
                       ".egress_approach = %d, .maneuvers = 0x%x, .n_nodes = %d,\n"
                       "        .node_x_cm = { %s }, .node_y_cm = { %s },\n"
                       "        .n_connections = %d, .conn = { %s } }," % (
                           L["lane_id"], L["direction"], L["lane_type"], L["ingress"], L["egress"],
                           L["maneuvers"], len(L["nodes"]), nx, ny, len(L["conns"]), cs))
        out.append("    },")
        out.append("  },")
    out.append("};")
    return "\n".join(out) + "\n"


def emit_spat(varname, states):
    out = ["static const its_spat_intersection_t %s[] = {" % varname]
    for s in states:
        sts = []
        for ms in s["states"][:48]:
            ev = ms["state-time-speed"][0]
            t = ev.get("timing")
            U = 36001
            if t:
                vals = (t.get("startTime", U), t["minEndTime"], t.get("maxEndTime", U), t.get("likelyTime", U))
            else:
                vals = (U, U, U, U)
            ph = ["unavailable", "dark", "stop-Then-Proceed", "stop-And-Remain", "pre-Movement",
                  "permissive-Movement-Allowed", "protected-Movement-Allowed", "permissive-clearance",
                  "protected-clearance", "caution-Conflicting-Traffic"].index(ev["eventState"])
            sts.append("{ .signal_group = %d, .phase = %d, .start_time = %d, .min_end_time = %d, "
                       ".max_end_time = %d, .likely_time = %d }" % ((ms["signalGroup"], ph) + vals))
        out.append("  { .region = %d, .id = %d, .revision = %d, .name = %s, .has_moy = %d, .moy = %d, "
                   ".has_timestamp = %d, .timestamp_ms = %d, .n_states = %d,\n    .states = {\n      %s\n    } }," % (
                       s["id"].get("region", 0), s["id"]["id"], s["revision"], cstr(s.get("name", "")),
                       int(s.get("moy", 527040) < 527040), (s.get("moy", 0) if s.get("moy", 527040) < 527040 else 0), int("timeStamp" in s), s.get("timeStamp", 0),
                       len(sts), ",\n      ".join(sts)))
    out.append("};")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# Message content
# ---------------------------------------------------------------------------
def vec_pos(lat, lon, alt=11000):
    return dict(latitude=lat, longitude=lon,
                positionConfidenceEllipse=dict(semiMajorConfidence=100, semiMinorConfidence=50, semiMajorOrientation=900),
                altitude=dict(altitudeValue=alt, altitudeConfidence="alt-000-20"))


def bvc_hf_v1(heading, speed, optional=False):
    d = dict(heading=dict(headingValue=heading, headingConfidence=10),
             speed=dict(speedValue=speed, speedConfidence=3),
             driveDirection="forward",
             vehicleLength=dict(vehicleLengthValue=45, vehicleLengthConfidenceIndication="noTrailerPresent"),
             vehicleWidth=18,
             longitudinalAcceleration=dict(longitudinalAccelerationValue=5, longitudinalAccelerationConfidence=3),
             curvature=dict(curvatureValue=0, curvatureConfidence="unavailable"),
             curvatureCalculationMode="yawRateUsed",
             yawRate=dict(yawRateValue=0, yawRateConfidence="unavailable"))
    if optional:
        d.update(lanePosition=2, performanceClass=1,
                 verticalAcceleration=dict(verticalAccelerationValue=1, verticalAccelerationConfidence=2),
                 steeringWheelAngle=dict(steeringWheelAngleValue=-20, steeringWheelAngleConfidence=5))
    return ("basicVehicleContainerHighFrequency", d)


def bvc_hf_v2(heading, speed, optional=False):
    d = dict(heading=dict(headingValue=heading, headingConfidence=10),
             speed=dict(speedValue=speed, speedConfidence=3),
             driveDirection="forward",
             vehicleLength=dict(vehicleLengthValue=45, vehicleLengthConfidenceIndication="noTrailerPresent"),
             vehicleWidth=18,
             longitudinalAcceleration=dict(value=5, confidence=3),
             curvature=dict(curvatureValue=0, curvatureConfidence="unavailable"),
             curvatureCalculationMode="yawRateUsed",
             yawRate=dict(yawRateValue=0, yawRateConfidence="unavailable"))
    if optional:
        d.update(lanePosition=2, performanceClass=1,
                 verticalAcceleration=dict(value=1, confidence=2),
                 steeringWheelAngle=dict(steeringWheelAngleValue=-20, steeringWheelAngleConfidence=5))
    return ("basicVehicleContainerHighFrequency", d)


def cam_msg(sid, stype, lat, lon, hf, v2=False, low=None, pos=None, ver=2):
    pos = pos or vec_pos(lat, lon)
    if v2:
        pos = dict(latitude=lat, longitude=lon,
                   positionConfidenceEllipse=dict(semiMajorAxisLength=100, semiMinorAxisLength=50, semiMajorAxisOrientation=900),
                   altitude=dict(altitudeValue=11000, altitudeConfidence="alt-000-20"))
        pl = "camParameters"
        body = dict(generationDeltaTime=4711,
                    camParameters=dict(basicContainer=dict(stationType=stype, referencePosition=pos),
                                       highFrequencyContainer=hf))
        if low:
            body["camParameters"]["lowFrequencyContainer"] = low
        return dict(header=dict(protocolVersion=ver, messageId=2, stationId=sid), cam=body)
    body = dict(generationDeltaTime=4711,
                camParameters=dict(basicContainer=dict(stationType=stype, referencePosition=pos),
                                   highFrequencyContainer=hf))
    if low:
        body["camParameters"]["lowFrequencyContainer"] = low
    return dict(header=dict(protocolVersion=ver, messageID=2, stationID=sid), cam=body)


def denm_v1(sid, seq, lat, lon, cause=None, term=None, optional_mgmt=True):
    m = dict(actionID=dict(originatingStationID=sid, sequenceNumber=seq), detectionTime=700000000000,
             referenceTime=700000001000, eventPosition=vec_pos(lat, lon), stationType=5)
    if term is not None:
        m["termination"] = term
    if optional_mgmt:
        m.update(relevanceDistance="lessThan500m", relevanceTrafficDirection="upstreamTraffic",
                 validityDuration=1800, transmissionInterval=500)
    d = dict(management=m)
    if cause:
        d["situation"] = dict(informationQuality=5, eventType=dict(causeCode=cause[0], subCauseCode=cause[1]))
        d["location"] = dict(traces=[[dict(pathPosition=dict(deltaLatitude=10, deltaLongitude=-12, deltaAltitude=0),
                                           pathDeltaTime=50)]])
    return dict(header=dict(protocolVersion=2, messageID=1, stationID=sid), denm=d)


def denm_v2(sid, seq, lat, lon, cause=None, term=None, optional_mgmt=True):
    m = dict(actionId=dict(originatingStationId=sid, sequenceNumber=seq), detectionTime=700000000000,
             referenceTime=700000001000,
             eventPosition=dict(latitude=lat, longitude=lon,
                                positionConfidenceEllipse=dict(semiMajorConfidence=100, semiMinorConfidence=50,
                                                               semiMajorOrientation=900),
                                altitude=dict(altitudeValue=11000, altitudeConfidence="alt-000-20")),
             stationType=5)
    if term is not None:
        m["termination"] = term
    if optional_mgmt:
        m.update(awarenessDistance="lessThan500m", trafficDirection="sameAsReferenceDirection-upstreamOfReferencePosition", validityDuration=1800,
                 transmissionInterval=500)
    d = dict(management=m)
    if cause:
        d["situation"] = dict(informationQuality=5, eventType=dict(ccAndScc=(cause[2], cause[1])))
    return dict(header=dict(protocolVersion=2, messageId=1, stationId=sid), denm=d)


# --- MAP / SPAT content ---------------------------------------------------
def bits(s):
    n = len(s)
    v = int(s, 2) << (-n % 8)
    return (v.to_bytes((n + 7) // 8, "big"), n)


def mk_lane(lid, nodes, direction="10", ltype="vehicle", ingress=None, egress=None, mnv=None,
            conns=None, name=None, regional=False, **extra):
    lt = {"vehicle": ("vehicle", bits("00000000")),
          "crosswalk": ("crosswalk", bits("0" * 16)),
          "bikeLane": ("bikeLane", bits("0" * 16)),
          "sidewalk": ("sidewalk", bits("0" * 16)),
          "median": ("median", bits("0" * 16)),
          "striping": ("striping", bits("0" * 16)),
          "trackedVehicle": ("trackedVehicle", bits("0" * 16)),
          "parking": ("parking", bits("0" * 16))}[ltype]
    attrs = dict(directionalUse=bits(direction), sharedWith=bits("0000000000"), laneType=lt)
    if regional:
        attrs["regional"] = dict(regionId=1, regExtValue=b"\xde\xad\xbe")
    l = dict(laneID=lid, laneAttributes=attrs, nodeList=("nodes", nodes))
    if name:
        l["name"] = name
    if ingress is not None:
        l["ingressApproach"] = ingress
    if egress is not None:
        l["egressApproach"] = egress
    if mnv is not None:
        l["maneuvers"] = bits(mnv)
    if conns:
        l["connectsTo"] = conns
    l.update(extra)
    return l


def xy(kind, x, y, **attrs):
    n = dict(delta=(kind, dict(x=x, y=y)))
    if attrs:
        n["attributes"] = attrs
    return n


def conn(lane, sg=None, mnv=None, remote=None, uclass=None, cid=None):
    c = dict(connectingLane=dict(lane=lane))
    if mnv:
        c["connectingLane"]["maneuver"] = bits(mnv)
    if sg is not None:
        c["signalGroup"] = sg
    if remote:
        c["remoteIntersection"] = remote
    if uclass is not None:
        c["userClass"] = uclass
    if cid is not None:
        c["connectionID"] = cid
    return c


def map_msg(sid, inters, **extra):
    m = dict(msgIssueRevision=3, intersections=inters)
    m.update(extra)
    return dict(header=dict(protocolVersion=2, messageID=5, stationID=sid), map=m)


def spat_msg(sid, states, **extra):
    s = dict(intersections=states)
    s.update(extra)
    return dict(header=dict(protocolVersion=2, messageID=4, stationID=sid), spat=s)


def mevent(state, **timing):
    e = dict(eventState=state)
    if timing:
        e["timing"] = timing
    return e


def mstate(sg, events, **kw):
    d = {"signalGroup": sg, "state-time-speed": events}
    d.update(kw)
    return d


def inter_state(region, iid, states, moy=None, ts=None, name=None, rev=1, **kw):
    d = dict(id=dict(id=iid), revision=rev, status=bits("0" * 16), states=states)
    if region is not None:
        d["id"]["region"] = region
    if moy is not None:
        d["moy"] = moy
    if ts is not None:
        d["timeStamp"] = ts
    if name:
        d["name"] = name
    d.update(kw)
    return d


def geometry(region, iid, name, lat, lon, lanes, **kw):
    g = dict(id=dict(id=iid), revision=2, refPoint=dict(lat=lat, long=lon), laneSet=lanes)
    if region is not None:
        g["id"]["region"] = region
    if name:
        g["name"] = name
    g.update(kw)
    return g


# ---------------------------------------------------------------------------
vectors = []   # dicts: name, frame, pdu_off, port, msgid, kind, expected text, ...
decls = []     # C declarations emitted before the table


def add_vec(name, frame, pdu_off, *, port, proto, msgid, sid, ht=5, hst=0, secured=False, pdu_len,
            err="ITS_OK", kind="NONE", exp=None, src=MAC_A, n_exp=0, note=""):
    vectors.append(dict(name=name, frame=frame, pdu_off=pdu_off, port=port, proto=proto, msgid=msgid, sid=sid,
                        ht=ht, hst=hst, secured=secured, pdu_len=pdu_len, err=err, kind=kind, exp=exp, src=src,
                        n_exp=n_exp, note=note))


def finish_msg(name, codec, typ, msg, port, *, kind, exp=None, n_exp=0, **fk):
    pdu = codec.encode(typ, msg)
    h = msg["header"]
    sid = h.get("stationID", h.get("stationId"))
    proto = h["protocolVersion"]
    msgid = h.get("messageID", h.get("messageId"))
    secured = fk.pop("secured", None)
    src = fk.get("src", MAC_A)
    frame, off = build_frame(pdu, port, secured=secured, **fk)
    add_vec(name, frame, off, port=port, proto=proto, msgid=msgid, sid=sid, ht=fk.get("ht", 5),
            hst=fk.get("hst", 0), secured=secured is not None, pdu_len=len(pdu), kind=kind, exp=exp, src=src,
            n_exp=n_exp)
    return pdu


def cam_exp(sid, stype, lat, lon, speed, heading):
    return "{ .station_id = %du, .station_type = %d, .lat = %d, .lon = %d, .speed = %d, .heading = %d }" % (
        sid, stype, lat, lon, speed, heading)


def denm_exp(sid, seq, cause, sub, lat, lon, term):
    return "{ .station_id = %du, .sequence_number = %d, .cause_code = %d, .sub_cause_code = %d, .lat = %d, .lon = %d, .termination = %d }" % (
        sid, seq, cause, sub, lat, lon, int(term))


# ---- CAM -----------------------------------------------------------------
LAT, LON = 520123456, 134567890
finish_msg("cam_v1_car", V1, "CAM", cam_msg(0x11111111, 5, LAT, LON, bvc_hf_v1(1234, 1389)), 2001, kind="CAM",
           exp=cam_exp(0x11111111, 5, LAT, LON, 1389, 1234))
finish_msg("cam_v1_car_optionals_gbc_btpa", V1, "CAM",
           cam_msg(0x11111112, 5, -337000000, -1500000000, bvc_hf_v1(0, 0, True)), 2001, kind="CAM", ht=4, hst=0,
           btp="A", qos=False, exp=cam_exp(0x11111112, 5, -337000000, -1500000000, 0, 0))
finish_msg("cam_v1_unavailable", V1, "CAM",
           cam_msg(0x11111113, 4, LAT, LON, bvc_hf_v1(3601, 16383)), 2001, kind="CAM", ht=5, hst=1, src=MAC_B,
           exp=cam_exp(0x11111113, 4, LAT, LON, 16383, 3601))
finish_msg("cam_v1_rsu", V1, "CAM",
           cam_msg(0x11111114, 15, LAT, LON, ("rsuContainerHighFrequency", {})), 2001, kind="CAM",
           exp=cam_exp(0x11111114, 15, LAT, LON, 16383, 3601))
finish_msg("cam_v1_ext_additions", V1X, "CAM",
           cam_msg(0x11111115, 6, LAT, LON, bvc_hf_v1(2500, 800)), 2001, kind="CAM",
           exp=cam_exp(0x11111115, 6, LAT, LON, 800, 2500))
# BasicContainer with extension additions present
_m = cam_msg(0x11111116, 6, LAT, LON, bvc_hf_v1(2501, 801))
_m["cam"]["camParameters"]["basicContainer"].update(futX=69999, futY=True)
finish_msg("cam_v1_ext_additions_present", V1X, "CAM", _m, 2001, kind="CAM",
           exp=cam_exp(0x11111116, 6, LAT, LON, 801, 2501))
finish_msg("cam_v1_secured", V1, "CAM", cam_msg(0x11111117, 5, LAT, LON, bvc_hf_v1(1800, 2222)), 2001, kind="CAM",
           secured="signed", exp=cam_exp(0x11111117, 5, LAT, LON, 2222, 1800))
finish_msg("cam_v2_bus", V2, "CAM", cam_msg(0x22222221, 6, 485000000, 91000000, bvc_hf_v2(900, 500, True), v2=True),
           2001, kind="CAM", exp=cam_exp(0x22222221, 6, 485000000, 91000000, 500, 900))
finish_msg("cam_v2_secured_qos", V2, "CAM", cam_msg(0x22222222, 8, LAT, LON, bvc_hf_v2(3599, 10), v2=True),
           2001, kind="CAM", secured="signed", exp=cam_exp(0x22222222, 8, LAT, LON, 10, 3599))
finish_msg("cam_v2_unsecured_content", V2, "CAM", cam_msg(0x22222223, 5, LAT, LON, bvc_hf_v2(1, 1), v2=True),
           2001, kind="CAM", secured="unsecured", exp=cam_exp(0x22222223, 5, LAT, LON, 1, 1), tail=b"\x00" * 7)

# ---- DENM ----------------------------------------------------------------
finish_msg("denm_v1_accident", V1, "DENM", denm_v1(0x33333331, 77, 520200000, 134000000, cause=(2, 1)), 2002,
           kind="DENM", ht=4, hst=1, exp=denm_exp(0x33333331, 77, 2, 1, 520200000, 134000000, False))
finish_msg("denm_v1_accident_cancel", V1, "DENM",
           denm_v1(0x33333331, 77, 520200000, 134000000, term="isCancellation"), 2002, kind="DENM",
           exp=denm_exp(0x33333331, 77, 0, 0, 520200000, 134000000, True))
finish_msg("denm_v1_minimal_mgmt", V1, "DENM",
           denm_v1(0x33333332, 1, 520200000, 134000000, cause=(27, 0), optional_mgmt=False), 2002, kind="DENM",
           exp=denm_exp(0x33333332, 1, 27, 0, 520200000, 134000000, False))
finish_msg("denm_v1_termination", V1, "DENM",
           denm_v1(0x33333333, 78, 520200000, 134000000, term="isCancellation"), 2002, kind="DENM",
           exp=denm_exp(0x33333333, 78, 0, 0, 520200000, 134000000, True))
finish_msg("denm_v1_secured_negation", V1, "DENM",
           denm_v1(0x33333334, 79, -10, 10, term="isNegation", optional_mgmt=False), 2002, kind="DENM",
           secured="signed", exp=denm_exp(0x33333334, 79, 0, 0, -10, 10, True))
_m = denm_v1(0x33333335, 80, 520200001, 134000001, cause=(91, 3))
_m["denm"]["management"]["futZ"] = "abc"
finish_msg("denm_v1_mgmt_ext_present", V1X, "DENM", _m, 2002, kind="DENM",
           exp=denm_exp(0x33333335, 80, 91, 3, 520200001, 134000001, False))
finish_msg("denm_v2_roadworks", V2, "DENM", denm_v2(0x44444441, 5, 520200002, 134000002, cause=(None, 7, "roadworks3")),
           2002, kind="DENM", exp=denm_exp(0x44444441, 5, 3, 7, 520200002, 134000002, False))
finish_msg("denm_v2_cancel", V2, "DENM", denm_v2(0x44444442, 6, 520200002, 134000002, term="isCancellation"),
           2002, kind="DENM", exp=denm_exp(0x44444442, 6, 0, 0, 520200002, 134000002, True))
finish_msg("denm_v2_wrongway_secured", V2, "DENM",
           denm_v2(0x44444443, 7, 520200003, 134000003, cause=(None, 2, "wrongWayDriving14"), optional_mgmt=False),
           2002, kind="DENM", secured="signed", exp=denm_exp(0x44444443, 7, 14, 2, 520200003, 134000003, False))
finish_msg("denm_v2_dangerous_curve", V2, "DENM",
           denm_v2(0x44444444, 8, 520200004, 134000004, cause=(None, 3, "hazardousLocation-DangerousCurve96")),
           2002, kind="DENM", exp=denm_exp(0x44444444, 8, 96, 3, 520200004, 134000004, False))

# ---- MAPEM ---------------------------------------------------------------
REF_A = (520000000, 130000000)
REF_B = (520100000, 130100000)
lanes_a = [
    mk_lane(1, [xy("node-XY1", 10, -20, localNode=["stopLine"]), xy("node-XY2", 0, -600),
                xy("node-XY3", 20, -1500), xy("node-XY4", -3000, 4000), xy("node-XY5", 100, 100),
                xy("node-XY6", -20000, 3000, dWidth=-5, dElevation=3)],
            direction="10", ingress=1, mnv="110000000000",
            conns=[conn(5, sg=3, mnv="100000000000", cid=1), conn(6, sg=3, mnv="010000000000"),
                   conn(7, sg=4, remote=dict(region=7, id=99), uclass=5)],
            name="Spur1", regional=True),
    mk_lane(2, [xy("node-XY1", -5, 5), xy("node-XY1", 3, 300)], direction="01", egress=2, ltype="vehicle"),
    mk_lane(3, [xy("node-XY2", 100, 100), xy("node-XY2", 200, -200)], direction="11", ltype="crosswalk",
            mnv="000000000011"),
    mk_lane(4, [xy("node-XY1", 1, 2), xy("node-XY1", 3, 4)], direction="10", ltype="bikeLane", ingress=0,
            conns=[conn(2, sg=1)]),
    mk_lane(5, [xy("node-XY1", 1, 2), xy("node-XY1", 3, 4)], direction="01", ltype="sidewalk"),
    mk_lane(6, [xy("node-XY1", 1, 2), xy("node-XY1", 3, 4)], direction="10", ltype="median"),
    mk_lane(7, [xy("node-XY1", 1, 2), xy("node-XY1", 3, 4)], direction="10", ltype="striping"),
    mk_lane(8, [xy("node-XY1", 1, 2), xy("node-XY1", 3, 4)], direction="10", ltype="trackedVehicle"),
    mk_lane(9, [xy("node-XY1", 1, 2), dict(delta=("node-LatLon", dict(lon=130001000, lat=520000500)))],
            direction="10", ltype="parking"),
    mk_lane(10, [xy("node-XY1", 7, 8, localNode=["stopLine", "mergePoint"], disabled=["doNotBlock"],
                    enabled=["whiteLine", "curbOnLeft"],
                    data=[("pathEndPointAngle", 17), ("laneCrownPointLeft", -3), ("laneAngle", 90),
                          ("speedLimits", [dict(type="vehicleMaxSpeed", speed=1389)]),
                          ("regional", [dict(regionId=4, regExtValue=b"\xaa")])],
                    regional=[dict(regionId=9, regExtValue=b"")]),
                xy("node-XY1", 1, 2)], direction="10"),
    mk_lane(11, [dict(delta=("regional", dict(regionId=3, regExtValue=b"\x01\x02"))), xy("node-XY1", 4, 5),
                 xy("node-XY1", 1, 1)], direction="10"),
]
geom_a = geometry(7, 12, "Kreuzung Mitte", REF_A[0], REF_A[1], lanes_a, laneWidth=320,
                  speedLimits=[dict(type="vehicleMaxSpeed", speed=1389), dict(type="vehicleMinSpeed", speed=100)],
                  regional=[dict(regionId=5, regExtValue=b"\x00\x01\x02")])
geom_a["refPoint"]["elevation"] = 1234
geom_a["refPoint"]["regional"] = [dict(regionId=2, regExtValue=b"\xff")]
geom_b = geometry(7, 13, "B", REF_B[0], REF_B[1], [
    mk_lane(1, [xy("node-XY1", 0, 0), xy("node-XY1", 0, 50)], ingress=3, mnv="001000000000",
            conns=[conn(2, sg=8)]),
    mk_lane(2, [xy("node-XY1", 100, 0), xy("node-XY1", 0, -50)], direction="01", egress=3),
    dict(laneID=3, laneAttributes=dict(directionalUse=bits("10"), sharedWith=bits("0000000000"),
                                       laneType=("vehicle", bits("00000000"))),
         nodeList=("computed", dict(referenceLaneId=1, offsetXaxis=("small", 300), offsetYaxis=("large", -20000),
                                    rotateXY=100, scaleXaxis=-5)),
         connectsTo=[conn(2, sg=9)], overlays=[1, 2]),
    mk_lane(4, [xy("node-XY1", 5, 5), xy("node-XY1", 6, 6)], direction="10", ingress=4, mnv="000100000000",
            conns=[conn(2, sg=10)]),
])
geom_b["preemptPriorityData"] = [dict(zone=dict(regionId=1, regExtValue=b"\x11\x22"))]
geom_b["regional"] = [dict(regionId=1, regExtValue=b"")]
geom_noname = geometry(None, 14, None, 480000000, 90000000,
                       [mk_lane(1, [xy("node-XY1", 1, 1), xy("node-XY1", 2, 2)], ingress=1,
                                conns=[conn(2, sg=1)]), mk_lane(2, [xy("node-XY1", 1, 1), xy("node-XY1", 2, 2)],
                                                                direction="01")])

finish_msg("mapem_two_intersections", DSRC, "MAPEM", map_msg(0x55555551, [geom_a, geom_b], timeStamp=300000,
                                                           layerType="intersectionData", layerID=7),
           2003, kind="MAP", exp=emit_map("exp_mapem_two_intersections", [geom_a, geom_b]), n_exp=2)
finish_msg("mapem_secured_noregion", DSRC, "MAPEM", map_msg(0x55555552, [geom_noname]), 2003, kind="MAP",
           secured="signed", exp=emit_map("exp_mapem_secured_noregion", [geom_noname]), n_exp=1, ht=4, hst=0)

# Later-edition MAP with extension additions everywhere
fut = dict(futA=99999, futB=True, futC="zz")
lanes_f = [
    mk_lane(1, [dict(delta=("node-XY1", dict(x=10, y=10)), attributes=dict(localNode=["stopLine"], **fut), **fut),
                dict(delta=("node-XY2", dict(x=20, y=-20)), **fut)],
            direction="10", ingress=2, mnv="100000000000", conns=[conn(2, sg=1)], **fut),
    dict(laneID=2, laneAttributes=dict(directionalUse=bits("01"), sharedWith=bits("0000000000"),
                                       laneType=("futureLane", bits("1" * 21))),
         nodeList=("futureNodes", b"\x01\x02\x03"), **fut),
    dict(laneID=3, laneAttributes=dict(directionalUse=bits("10"), sharedWith=bits("0000000000"),
                                       laneType=("vehicle", bits("00000000"))),
         nodeList=("nodes", [dict(delta=("node-XY1", dict(x=1, y=2)),
                                  attributes=dict(localNode=["futureAttr2"], disabled=["futureSeg"],
                                                  data=[("futureAngle", 5)])),
                             dict(delta=("node-XY1", dict(x=3, y=4)))]),
         connectsTo=[conn(1, sg=2)]),
    dict(laneID=4, laneAttributes=dict(directionalUse=bits("10"), sharedWith=bits("0000000000"),
                                       laneType=("vehicle", bits("00000000"))),
         nodeList=("computed", dict(referenceLaneId=1, offsetXaxis=("small", 3), offsetYaxis=("small", 4), **fut)),
         **fut),
    mk_lane(5, [xy("node-XY1", 7, 7), xy("node-XY1", 8, 8)], direction="10", ingress=5, conns=[conn(1, sg=5)]),
]
geom_f1 = geometry(8, 1, "Zukunft", 520000000, 130000000, lanes_f, **fut)
geom_f1["refPoint"].update(fut)
geom_f2 = geometry(8, 2, "Zukunft2", 520000100, 130000100, [mk_lane(1, [xy("node-XY1", 1, 1), xy("node-XY1", 2, 2)],
                                                                     ingress=1, conns=[conn(2, sg=1)])], **fut)
geom_f2["preemptPriorityData"] = [dict(zone=dict(regionId=1, regExtValue=b"\x11\x22"))]
finish_msg("mapem_future_extensions", DSRCF, "MAPEM",
           map_msg(0x55555553, [geom_f1, geom_f2], layerType="futureLayer", **fut), 2003, kind="MAP",
           exp=emit_map("exp_mapem_future_extensions", [geom_f1, geom_f2]), n_exp=2)

# MAP with roadSegments / dataParameters / restrictionList after the intersections (ignored)
seg = dict(id=1, revision=1, refPoint=dict(lat=1, long=2), roadLaneSet=[mk_lane(1, [xy("node-XY1", 1, 1), xy("node-XY1", 2, 2)])])
finish_msg("mapem_with_trailing_containers", DSRC, "MAPEM",
           map_msg(0x55555554, [geom_noname], roadSegments=[seg],
                   dataParameters=dict(processMethod="abc", geoidUsed="WGS84"),
                   restrictionList=[dict(id=1, users=[("basicType", "pedestrians")])],
                   regional=[dict(regionId=1, regExtValue=b"\x01")]),
           2003, kind="MAP", exp=emit_map("exp_mapem_with_trailing_containers", [geom_noname]), n_exp=1)

# Many lanes / nodes / connections: truncation to the store limits
many_lanes = []
for i in range(1, 71):
    nn = 2 + (i % 10)
    many_lanes.append(mk_lane(i, [xy("node-XY2", 10 + i, 20 + (k % 5)) for k in range(nn)],
                              direction="10" if i % 2 else "01", ingress=(i % 15) + 1 if i % 2 else None,
                              mnv="1" + "0" * 11,
                              conns=[conn((i + k) % 255, sg=k + 1) for k in range(1 + i % 7)] if i % 2 else None))
geom_many = geometry(9, 1000, "Viele Spuren", 500000000, 100000000, many_lanes)
mm = map_msg(0x55555555, [geom_many])
finish_msg("mapem_many_lanes", DSRC, "MAPEM", mm, 2003, kind="MAP", ht=4, hst=2,
           exp=emit_map("exp_mapem_many_lanes", [geom_many]), n_exp=1)
# 4 intersections in one message (decoder output limit test is done in C)
geoms4 = [geometry(10, 1 + i, "N%d" % i, 500000000 + i * 1000, 100000000, [
    mk_lane(1, [xy("node-XY1", 1, 1), xy("node-XY1", 2, 2)], ingress=1, conns=[conn(2, sg=i + 1)]),
    mk_lane(2, [xy("node-XY1", 3, 3), xy("node-XY1", 4, 4)], direction="01")]) for i in range(4)]
finish_msg("mapem_four_intersections", DSRC, "MAPEM", map_msg(0x55555556, geoms4), 2003, kind="MAP",
           exp=emit_map("exp_mapem_four_intersections", geoms4), n_exp=4)

# ---- SPATEM --------------------------------------------------------------
sp1 = inter_state(7, 12, [
    mstate(1, [mevent("stop-And-Remain", startTime=100, minEndTime=200, maxEndTime=300, likelyTime=250)]),
    mstate(2, [mevent("protected-Movement-Allowed", minEndTime=500, likelyTime=520),
               mevent("protected-clearance", minEndTime=540)]),
    mstate(3, [mevent("pre-Movement", minEndTime=36000)]),
    mstate(4, [mevent("caution-Conflicting-Traffic")]),
    mstate(5, [mevent("dark", minEndTime=36001)]),
    mstate(6, [mevent("permissive-Movement-Allowed", startTime=0, minEndTime=0, maxEndTime=36000, likelyTime=1,
                      confidence=15, nextTime=35999)]),
    mstate(7, [mevent("stop-Then-Proceed", minEndTime=1)]),
    mstate(8, [mevent("permissive-clearance", minEndTime=2)]),
    mstate(9, [mevent("unavailable")]),
], moy=527039, ts=59999, name="Kreuzung Mitte", rev=5)
sp2 = inter_state(7, 13, [mstate(8, [mevent("protected-Movement-Allowed", minEndTime=100)])], moy=100, ts=0)
finish_msg("spatem_two_intersections", DSRC, "SPATEM", spat_msg(0x66666661, [sp1, sp2], timeStamp=1234,
                                                              name="SPAT Name"),
           2004, kind="SPAT", exp=emit_spat("exp_spatem_two_intersections", [sp1, sp2]), n_exp=2)
sp3 = inter_state(None, 14, [mstate(1, [mevent("protected-Movement-Allowed", minEndTime=1800, likelyTime=1800)])],
                  moy=3600 + 5, ts=61234)
finish_msg("spatem_secured_noregion", DSRC, "SPATEM", spat_msg(0x66666662, [sp3]), 2004, kind="SPAT",
           secured="signed", exp=emit_spat("exp_spatem_secured_noregion", [sp3]), n_exp=1)
sp4 = inter_state(None, 15, [mstate(1, [mevent("stop-And-Remain")]), mstate(2, [mevent("protected-clearance")])])
finish_msg("spatem_no_timing_no_clock", DSRC, "SPATEM", spat_msg(0x66666663, [sp4]), 2004, kind="SPAT",
           exp=emit_spat("exp_spatem_no_timing_no_clock", [sp4]), n_exp=1)

# Later-edition SPAT: extensions, advisory speeds, maneuver assist, enabled lanes, regional
adv = [dict(type="greenwave", speed=50, confidence="prec1ms", distance=100, **{"class": 3},
            regional=[dict(regionId=1, regExtValue=b"\x01")], **fut), dict(type="none")]
cma = [dict(connectionID=1, queueLength=10, availableStorageLength=20, waitOnStop=True, pedBicycleDetect=False,
            regional=[dict(regionId=2, regExtValue=b"\x02\x03")], **fut), dict(connectionID=2)]
sp_f = inter_state(11, 1, [
    mstate(1, [dict(eventState="stop-And-Remain", timing=dict(startTime=1, minEndTime=2, maxEndTime=3, likelyTime=4,
                                                              confidence=2, nextTime=9),
                   speeds=adv, regional=[dict(regionId=1, regExtValue=b"")], **fut),
               mevent("protected-Movement-Allowed", minEndTime=77)],
           movementName="Name", maneuverAssistList=cma, regional=[dict(regionId=1, regExtValue=b"\x09")], **fut),
    mstate(2, [mevent("permissive-clearance", minEndTime=44, likelyTime=45)], **fut),
], moy=1000, ts=1000, name="FutureSPAT", enabledLanes=[1, 2, 3], maneuverAssistList=cma,
    regional=[dict(regionId=7, regExtValue=b"\x01\x02")], **fut)
sp_f2 = inter_state(11, 2, [mstate(3, [mevent("dark", minEndTime=5)])], moy=1000, ts=1100, **fut)
finish_msg("spatem_future_extensions", DSRCF, "SPATEM",
           spat_msg(0x66666664, [sp_f, sp_f2], timeStamp=5, name="N", regional=[dict(regionId=1, regExtValue=b"\x01")],
                    **fut),
           2004, kind="SPAT", exp=emit_spat("exp_spatem_future_extensions", [sp_f, sp_f2]), n_exp=2)

# SPAT with > 32 movement states
sp_many = inter_state(12, 1, [mstate(i % 255 + 1, [mevent("protected-Movement-Allowed", minEndTime=i * 10)])
                              for i in range(60)], moy=61, ts=5000)
finish_msg("spatem_many_states", DSRC, "SPATEM", spat_msg(0x66666665, [sp_many]), 2004, kind="SPAT", ht=4,
           exp=emit_spat("exp_spatem_many_states", [sp_many]), n_exp=1)

# invalid MinuteOfTheYear (527040): no clock may be derived
sp_badmoy = inter_state(13, 1, [mstate(1, [mevent("protected-Movement-Allowed", minEndTime=50)])], moy=527040, ts=100)
finish_msg("spatem_invalid_moy", DSRC, "SPATEM", spat_msg(0x66666666, [sp_badmoy]), 2004, kind="SPAT",
           exp=emit_spat("exp_spatem_invalid_moy", [sp_badmoy]), n_exp=1)

# ---- Frame level extras ----------------------------------------------------
# GN beacon (no payload): HT 1
beacon_common = bytes([0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x01, 0x00])
frame = wlan() + LLC + bytes([0x11, 0, 0x1e, 1]) + beacon_common + lpv()
add_vec("gn_beacon", frame, 0, port=0, proto=0, msgid=0, sid=0, ht=1, hst=0, pdu_len=0)
# GUC with BTP-A carrying a CAM (ht 2)
finish_msg("cam_v1_guc_btpa", V1, "CAM", cam_msg(0x11111118, 5, LAT, LON, bvc_hf_v1(10, 20)), 2001, kind="CAM",
           ht=2, hst=0, btp="A", exp=cam_exp(0x11111118, 5, LAT, LON, 20, 10))
# 4-address / HT-control frame (ToDS=FromDS=1, order bit)
finish_msg("cam_v1_4addr_htc", V1, "CAM", cam_msg(0x11111119, 5, LAT, LON, bvc_hf_v1(10, 20)), 2001, kind="CAM",
           exp=cam_exp(0x11111119, 5, LAT, LON, 20, 10))
_v = vectors[-1]
_fr = bytearray(_v["frame"])
_fr[1] = 0x83 | 0x00                               # ToDS|FromDS, order bit (HTC)
_fr = bytes(_fr[:24]) + b"\xaa" * 6 + bytes(_fr[24:26]) + b"\x00\x00\x00\x00" + bytes(_fr[26:])
_v["frame"] = _fr
_v["pdu_off"] += 10


def hdr_only(name, frame, err, port=0):
    add_vec(name, frame, 0, port=port, proto=0, msgid=0, sid=0, pdu_len=0, err=err)


good = vectors[0]["frame"]
good_off = vectors[0]["pdu_off"]
# ethertype IPv6
f = bytearray(good)
f[26 + 6] = 0x86
f[26 + 7] = 0xdd
hdr_only("neg_ethertype_ipv6", bytes(f), "ITS_ERR_NOT_GN")
# management frame (beacon)
f = bytearray(good)
f[0] = 0x80
hdr_only("neg_mgmt_frame", bytes(f), "ITS_ERR_NOT_GN")
# QoS null data
f = bytearray(good)
f[0] = 0xc8
hdr_only("neg_qos_null", bytes(f), "ITS_ERR_NOT_GN")
hdr_only("neg_too_short_wlan", good[:20], "ITS_ERR_TOO_SHORT")
hdr_only("neg_too_short_gn", good[:26 + 8 + 2], "ITS_ERR_TOO_SHORT")
f = bytearray(good)
f[26 + 8] = 0x13                                   # basic NH = 3
hdr_only("neg_basic_nh3", bytes(f), "ITS_ERR_GN_UNSUPPORTED")
f = bytearray(good)
f[26 + 8] = 0x01                                   # basic header version 0
hdr_only("neg_basic_version0", bytes(f), "ITS_ERR_GN_UNSUPPORTED")
f = bytearray(good)
f[26 + 12 + 1] = 0x70                              # common header HT = 7
hdr_only("neg_common_ht7", bytes(f), "ITS_ERR_GN_UNSUPPORTED")
f = bytearray(good)
f[26 + 12 + 4] = 0xff                              # PL larger than frame
hdr_only("neg_pl_too_large", bytes(f), "ITS_ERR_TOO_SHORT")
f = bytearray(good)
f[26 + 12 + 4:26 + 12 + 6] = b"\x00\x02"           # PL smaller than BTP header
hdr_only("neg_pl_lt_btp", bytes(f), "ITS_ERR_BTP")
f = bytearray(good)
f[26 + 12 + 4:26 + 12 + 6] = b"\x00\x08"           # PL = BTP + 4: no room for ITS header
hdr_only("neg_pl_lt_itshdr", bytes(f), "ITS_ERR_DECODE")
# secured with garbage / unsupported content
sec = [v for v in vectors if v["name"] == "cam_v1_secured"][0]
f = bytearray(sec["frame"])
base = 26 + 8 + 4
f[base] = 0x04                                      # Ieee1609Dot2Data protocolVersion != 3
hdr_only("neg_secured_badversion", bytes(f), "ITS_ERR_SECURITY")
f = bytearray(sec["frame"])
f[base + 1] = 0x82                                  # encryptedData
hdr_only("neg_secured_encrypted", bytes(f), "ITS_ERR_SECURITY")
f = bytearray(sec["frame"])
f[base + 3] = 0x20                                  # SignedDataPayload without data (extDataHash only)
hdr_only("neg_secured_hashonly", bytes(f), "ITS_ERR_SECURITY")
f = bytearray(sec["frame"])
f[base + 5] = 0x7f                                  # unsecuredData length too large
hdr_only("neg_secured_len_overrun", bytes(f), "ITS_ERR_SECURITY")
hdr_only("neg_secured_truncated", sec["frame"][:base + 10], "ITS_ERR_SECURITY")

# ---------------------------------------------------------------------------
# Emit vectors.h
# ---------------------------------------------------------------------------
out = []
out.append("""/* vectors.h - GENERATED by test/gen_vectors.py (python3 -I gen_vectors.py). DO NOT EDIT.
 * Frames: 802.11 + LLC/SNAP + GeoNetworking + BTP + UPER (asn1tools), FCS removed.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "its.h"

typedef enum { VK_NONE = 0, VK_CAM, VK_DENM, VK_MAP, VK_SPAT } vec_kind_t;

typedef struct {
    const char *name;
    const uint8_t *frame;
    size_t len;
    its_err_t err;            /* expected its_parse_frame() result */
    bool secured;
    uint8_t ht, hst;
    uint16_t port;
    uint8_t proto, msg_id;
    uint32_t station_id;
    size_t pdu_off, pdu_len;  /* offset of ItsPduHeader in frame */
    uint8_t src_mac[6];
    vec_kind_t kind;
    const its_cam_t *cam;
    const its_denm_t *denm;
    int n_map;
    const its_map_intersection_t *map;
    int n_spat;
    const its_spat_intersection_t *spat;
} test_vector_t;
""")
for v in vectors:
    out.append(c_bytes("frame_" + v["name"], v["frame"]))
for v in vectors:
    if v["kind"] == "CAM":
        out.append("static const its_cam_t exp_%s = %s;\n" % (v["name"], v["exp"]))
    elif v["kind"] == "DENM":
        out.append("static const its_denm_t exp_%s = %s;\n" % (v["name"], v["exp"]))
    elif v["kind"] in ("MAP", "SPAT"):
        out.append(v["exp"])
rows = []
for v in vectors:
    kind = {"NONE": "VK_NONE", "CAM": "VK_CAM", "DENM": "VK_DENM", "MAP": "VK_MAP", "SPAT": "VK_SPAT"}[v["kind"]]
    ptr = dict(cam="NULL", denm="NULL", map="NULL", spat="NULL")
    nm = ns = 0
    if v["kind"] == "CAM":
        ptr["cam"] = "&exp_" + v["name"]
    elif v["kind"] == "DENM":
        ptr["denm"] = "&exp_" + v["name"]
    elif v["kind"] == "MAP":
        ptr["map"] = "exp_" + v["name"]
        nm = v["n_exp"]
    elif v["kind"] == "SPAT":
        ptr["spat"] = "exp_" + v["name"]
        ns = v["n_exp"]
    rows.append('    { "%s", frame_%s, sizeof(frame_%s), %s, %d, %d, %d, %d, %d, %d, 0x%xu, %d, %d, '
                "{ %s }, %s, %s, %s, %d, %s, %d, %s }," % (
                    v["name"], v["name"], v["name"], v["err"], int(v["secured"]), v["ht"], v["hst"], v["port"],
                    v["proto"], v["msgid"], v["sid"], v["pdu_off"], v["pdu_len"],
                    ", ".join("0x%02x" % b for b in v["src"]), kind, ptr["cam"], ptr["denm"], nm, ptr["map"],
                    ns, ptr["spat"]))
out.append("static const test_vector_t g_vectors[] = {\n" + "\n".join(rows) + "\n};\n")
out.append("#define N_VECTORS (sizeof(g_vectors) / sizeof(g_vectors[0]))\n")

path = os.path.join(HERE, "vectors.h")
with open(path, "w") as fh:
    fh.write("\n".join(out))
print("wrote %s: %d vectors, %d bytes" % (path, len(vectors), os.path.getsize(path)))
