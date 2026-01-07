# Keypr - Master Project Overview

## Introduction

Keypr is a consent-first remote keyholding platform consisting of three components:

1. **Flutter Mobile App** (iOS, iPadOS, Android, Android tablets)
2. **Backend API** (getkeypr.com)
3. **ESP32 BLE Lock Box Device**

The platform enables **Subs** (device owners) to grant remote lock/unlock control to **Keyholders** while maintaining full consent through pre-configured rules set before each session.

---

## System Architecture

```
┌─────────────────┐      WiFi       ┌─────────────────┐
│    ESP32        │◄───────────────►│      API        │
│    Device       │                 │  (getkeypr.com) │
└────────▲────────┘                 └────────▲────────┘
         │ BLE                               │ HTTPS
         │                                   │
┌────────▼───────────────────────────────────▼────────┐
│                   Flutter App                        │
│  • UI/UX           • GPS collection                 │
│  • BLE bridge      • Media capture                  │
│  • WiFi config     • Offline queue                  │
└──────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibilities |
|-----------|-----------------|
| **ESP32 Device** | Physical lock mechanism, BLE communication, WiFi to API, local timer enforcement, tamper detection, e-paper display, reports actual state |
| **API** | Source of truth for intended state, all commands, business logic, users, sessions, contracts, history, messaging, community features |
| **Flutter App** | UI, BLE bridge when device offline, WiFi credential configuration, GPS collection, media handling, offline caching, action queue |

---

## ESP32 Device Specifications

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32 (BLE + WiFi) |
| **Display** | 1.54" e-paper (status, time remaining, Keyholder messages) |
| **Buttons** | Physical: Open lid button + Emergency pattern input |
| **Lock Mechanism** | Servo motor (current design, solenoid under consideration) |
| **Sensors** | Reed switch (lid state), comprehensive tamper detection |
| **Audio** | None - silent operation |
| **Power** | Rechargeable LiPo + USB-C (operates while charging) |
| **Battery Target** | 2+ weeks standby |
| **Connectivity** | BLE (primary app connection) + WiFi (direct API, user-configured) |
| **Form Factor** | Wearable/portable, holds keys + small items |
| **Tamper Response** | Alert only (notify Keyholder, log event) |

---

## Core Concepts

### User Roles

- **Sub**: Device owner who wears/uses the lock box. Sets rules before locking. Can request emergency unlock based on pre-configured settings.
- **Keyholder (KH)**: Remote controller who manages lock/unlock. Can send messages, grant breaks, modify session terms.
- **Switch**: User who operates as both Sub and Keyholder (different relationships).

### Relationships

- **Many-to-many**: A Sub can have multiple Keyholders; a Keyholder can manage multiple Subs.
- **Consent-first**: Sub configures all rules (emergency unlock, breaks, etc.) BEFORE a session begins.

### Sessions & Contracts

Full BDSM contract support including:
- Duration (open-ended or timed)
- Goals, tasks, rewards, punishments
- Extensions
- Hygiene/bathroom breaks (scheduled + emergency + KH-granted)
- Emergency unlock configuration (immediate, delayed, or KH-required)

### Solo Mode

When not connected to a Keyholder:
- Self-lock timers
- Community Keyholder requests

---

## Premium Tier Structure

### Free Tier
- 1 device
- 1 Keyholder connection
- Basic timer sessions
- 7-day history retention
- Unlimited text messaging
- Basic profile

### Basic Tier
- Multiple Keyholders
- Advanced contracts (full terms, goals, rewards)
- Extended history retention
- Media messaging (photos, videos, voice)

### Pro Tier
- All Basic features
- API integrations (third-party site connections)
- Geofencing alerts
- Detailed analytics and stats
- Verification badges
- Public cage checks on profile

### Billing
- Monthly + Annual (discounted)
- Referral rewards earn premium time
- Achievements can unlock features
- Keyholders can gift premium to Subs

---

## Key Features Summary

| Category | Features |
|----------|----------|
| **Authentication** | Email/password, Google, Apple Sign-In, biometric app lock |
| **Devices** | Unlimited registration, BLE discovery pairing, offline-capable with sync |
| **GPS** | Phone location logging, device last-known location, geofencing (premium) |
| **Contracts** | Duration, goals, tasks, rewards, punishments, extensions, scheduled breaks |
| **Emergency** | Sub-configured before session: immediate, delayed, or KH-required unlock |
| **Messaging** | Unlimited text; media for premium |
| **Social** | Profiles, find Keyholders, forums, groups, leaderboards, trust scores |
| **Achievements** | Optional public, can unlock premium features |
| **Privacy** | Full GDPR compliance, data export/deletion, theme choice |
| **Audit** | Full activity trail for all actions |
| **Notifications** | Essential only: lock/unlock, emergency, messages |

---

## Future Phases

- Home screen widgets
- Smartwatch companion app (Apple Watch, Wear OS)

---

## Feature PRD Index

The following PRDs detail each feature area:

1. `prd-01-core-infrastructure.md` - Project setup, API client, offline architecture
2. `prd-02-authentication.md` - Registration, login, biometrics, profile management
3. `prd-03-device-management.md` - BLE pairing, WiFi config, multi-device support
4. `prd-04-lock-control.md` - Lock/unlock commands, status sync, emergency unlock
5. `prd-05-gps-location.md` - Location tracking, last known, geofencing
6. `prd-06-sessions-contracts.md` - Sessions, contracts, breaks, timers
7. `prd-07-relationships.md` - Keyholder connections, permissions, favorites
8. `prd-08-messaging.md` - Chat, media sharing, verification photos
9. `prd-09-community-social.md` - Discovery, profiles, trust scores, forums
10. `prd-10-achievements.md` - Milestones, gamification, public/private
11. `prd-11-premium-monetization.md` - Tiers, payments, referrals, gifting
12. `prd-12-history-audit.md` - Event logging, history views, analytics
13. `prd-13-notifications.md` - Push notifications, preferences

---

## Implementation Phases

### Phase 1 - Foundation
- Core Infrastructure
- Authentication & User Management
- Device Management
- Lock Control

### Phase 2 - Core Experience
- Sessions & Contracts
- Relationships
- Messaging (text only)

### Phase 3 - Enhanced Features
- GPS & Location
- History & Audit
- Notifications

### Phase 4 - Social & Growth
- Community & Social
- Achievements & Gamification

### Phase 5 - Monetization
- Premium & Monetization
- Media Messaging (premium feature)

---

## Non-Goals (Out of Scope for V1)

- Home screen widgets
- Smartwatch companion app
- Third-party API integrations (Pro tier, later phase)
- Multiple language support (English only initially)
