# 🎨 Giao Diện Home Assistant - Phong Cách ChatGPT

## 📸 Mô Tả Giao Diện

### Layout Tổng Quan

```
┌─────────────────────────────────────────────────────┐
│  [☰] Home Assistant                                 │
├──────────┬──────────────────────────────────────────┤
│          │                                           │
│ 🏠 THIẾT BỊ│          💬 CHAT AREA                  │
│          │                                           │
│ 💡 Đèn   │   ┌────────────────────────┐            │
│ 🌡️ Cảm biến│   │ Bạn: Bật đèn phòng khách│            │
│ 🔌 Ổ cắm │   └────────────────────────┘            │
│          │                                           │
│ 📅 LỊCH HẸN│   ┌────────────────────────┐            │
│          │   │ Assistant: Đã bật đèn   │            │
│ 07:00 PM │   └────────────────────────┘            │
│ Bật đèn  │                                           │
│          │                                           │
│          │   [● ● ●] Đang xử lý...                  │
└──────────┴───────────────────────────────────────────┤
           │ ┌──────────────────────────────────┐    │
           │ │ Gửi tin nhắn... [🎤] [✈️]        │    │
           │ └──────────────────────────────────┘    │
           └─────────────────────────────────────────┘
```

## 🎨 Màu Sắc Chủ Đạo

### Palette

```
Màu nền chính:      #0c0c0f (Đen nhẹ)
Màu nền chat:       #212121 (Xám đậm)
Màu nền sidebar:    #171717 (Xám tối)
Màu accent:         #10a37f (Xanh lá ChatGPT)
Màu text chính:     #ececec (Trắng nhạt)
Màu text phụ:       #9b9b9b (Xám)
Màu border:         #303030 (Xám đậm)
```

## 🖼️ Các Thành Phần Giao Diện

### 1. Sidebar (Bên Trái)

- **Header**: Logo + tiêu đề "🏠 Home Assistant"
- **Device List**:
  - Card-based design
  - Hover effects
  - Quick action buttons
  - Status indicators
- **Schedule List**:
  - Timeline display
  - Color-coded status
  - Compact layout

### 2. Chat Area (Chính Giữa)

- **Message Bubbles**:
  - User messages: Bên phải, màu xám đậm (#2f2f2f)
  - Assistant messages: Bên trái, nền trong suốt
  - Rounded corners (24px)
  - Smooth animations
- **Empty State**:

  - Icon lớn ở giữa
  - Text hướng dẫn
  - Màu xám nhẹ

- **Loading State**:
  - 3 dots animation
  - Pulse effect
  - Màu xám

### 3. Input Box (Dưới Cùng)

- **Design**:

  - Fixed bottom
  - Rounded border (#303030)
  - Nền xám đậm (#2f2f2f)
  - Padding 12px

- **Components**:
  - Textarea: Auto-resize, max 200px
  - Microphone button: Icon mic, hover effect
  - Send button: Màu xanh lá (#10a37f)
  - Recording state: Nút đỏ + timer

## 🎬 Animations

### Slide In (Messages)

```css
@keyframes slideIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}
```

### Pulse (Recording Button)

```css
@keyframes pulse {
  0%,
  100% {
    opacity: 1;
  }
  50% {
    opacity: 0.8;
  }
}
```

### Ripple (Recording Effect)

```css
@keyframes ripple {
  0% {
    transform: scale(1);
    opacity: 0.8;
  }
  100% {
    transform: scale(1.3);
    opacity: 0;
  }
}
```

### Dot Pulse (Loading)

```css
@keyframes dotPulse {
  0%,
  60%,
  100% {
    opacity: 0.3;
    transform: scale(0.8);
  }
  30% {
    opacity: 1;
    transform: scale(1);
  }
}
```

## 📱 Responsive Design

### Desktop (> 1024px)

- Sidebar: 280px fixed
- Chat: Flexible width
- Full features visible

### Tablet (768px - 1024px)

- Sidebar: Toggle with button
- Chat: Full width when sidebar hidden
- Optimized touch targets

### Mobile (< 768px)

- Sidebar: Overlay when open
- Chat: Full width
- Compact spacing
- Larger touch areas

## 🎯 User Experience

### Hover Effects

- **Buttons**: Scale 1.05, color change
- **Device cards**: Border color change
- **Links**: Underline + color

### Focus States

- **Input**: Border color → accent
- **Buttons**: Box shadow accent color

### Disabled States

- **Opacity**: 0.4
- **Cursor**: not-allowed
- **No hover effects**

## 🌟 Special Features

### Microphone Button

- **Idle**: Gray icon
- **Hover**: Slight scale + background
- **Recording**: Red background + pulse + ripple effect
- **Timer**: Shows recording duration

### Send Button

- **Idle**: Gray (disabled if empty input)
- **Active**: Green background (#10a37f)
- **Hover**: Darker green (#0e8f6f)

### Sidebar Toggle

- **Icon**: Hamburger menu (3 lines)
- **Position**: Top-left fixed
- **Mobile only**: Hidden on desktop

## 💡 Design Principles

1. **Minimalism**: Ít yếu tố, nhiều không gian trắng
2. **Consistency**: Sử dụng cùng font, spacing, colors
3. **Clarity**: Text rõ ràng, hierarchy tốt
4. **Feedback**: Animations cho mọi interaction
5. **Accessibility**: Contrast tốt, focus states rõ ràng

## 🔤 Typography

```css
Font Family: 'Söhne', 'Inter', system-ui, sans-serif
Base Size: 16px

Headings:
- H1: 18px, weight 600
- H2: 12px, weight 600, uppercase

Body:
- Default: 15px
- Small: 13px
- Tiny: 11px

Line Height: 1.5 - 1.6
```

## 🎪 Interactive Elements

### Buttons

- **Padding**: 8-12px
- **Border-radius**: 8px
- **Transition**: 0.2s ease

### Cards

- **Padding**: 12-16px
- **Border-radius**: 12px
- **Border**: 1px solid #303030

### Inputs

- **Padding**: 10-12px
- **Border-radius**: 12px
- **Border**: 1px solid #303030
- **Focus**: Border → accent + shadow

---

**Giao diện này được thiết kế để:**

- ✅ Dễ sử dụng, trực quan
- ✅ Hiện đại, chuyên nghiệp
- ✅ Responsive trên mọi thiết bị
- ✅ Tối ưu performance
- ✅ Accessibility tốt
