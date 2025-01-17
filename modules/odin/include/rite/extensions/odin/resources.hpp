#pragma once

constexpr char DEFAULT_CSS[] = R"(
@import url('https://fonts.googleapis.com/css2?family=Lato:ital,wght@0,100;0,300;0,400;0,700;0,900;1,100;1,300;1,400;1,700;1,900&family=Poppins:ital,wght@0,100;0,200;0,300;0,400;0,500;0,600;0,700;0,800;0,900;1,100;1,200;1,300;1,400;1,500;1,600;1,700;1,800;1,900&display=swap');

:root {
    --primary: #226CE0;
    --primary-200: color-mix(in lch, var(--primary) 90%, white);
    --primary-300: color-mix(in lch, var(--primary) 80%, white);
    --primary-400: color-mix(in lch, var(--primary) 70%, white);
    --primary-500: color-mix(in lch, var(--primary) 60%, white);

    --primary-10: color-mix(in lch, var(--primary) 90%, black);
    --primary-20: color-mix(in lch, var(--primary) 80%, black);
    --primary-30: color-mix(in lch, var(--primary) 70%, black);
    --primary-40: color-mix(in lch, var(--primary) 60%, black);

    --secondary: #773344;
    --secondary-200: color-mix(in lch, var(--secondary) 90%, white);
    --secondary-300: color-mix(in lch, var(--secondary) 80%, white);
    --secondary-400: color-mix(in lch, var(--secondary) 70%, white);
    --secondary-500: color-mix(in lch, var(--secondary) 60%, white);
    --secondary-10:  color-mix(in lch, var(--secondary) 90%, black);
    --secondary-20:  color-mix(in lch, var(--secondary) 80%, black);
    --secondary-30:  color-mix(in lch, var(--secondary) 70%, black);
    --secondary-40:  color-mix(in lch, var(--secondary) 60%, black);


    --accent: #C36DCA;
    --accent-200: color-mix(in lch, var(--accent) 90%, white);
    --accent-300: color-mix(in lch, var(--accent) 80%, white);
    --accent-400: color-mix(in lch, var(--accent) 70%, white);
    --accent-500: color-mix(in lch, var(--accent) 60%, white);
    --accent-10:  color-mix(in lch, var(--accent) 90%, black);
    --accent-20:  color-mix(in lch, var(--accent) 80%, black);
    --accent-30:  color-mix(in lch, var(--accent) 70%, black);
    --accent-40:  color-mix(in lch, var(--accent) 60%, black);


    --background: #f1f1f1;
    --background-200: color-mix(in lch, var(--background) 90%, white);
    --background-300: color-mix(in lch, var(--background) 80%, white);
    --background-400: color-mix(in lch, var(--background) 70%, white);
    --background-500: color-mix(in lch, var(--background) 60%, white);
    --background-600: color-mix(in lch, var(--background) 50%, white);
    --background-700: color-mix(in lch, var(--background) 40%, white);
    --background-800: color-mix(in lch, var(--background) 30%, white);
    --background-900: color-mix(in lch, var(--background) 20%, white);

    --text-color: #0f0f0f;

    --spacing: 0.6em;
    --spacing-lg: calc(var(--spacing) * 1.5);
    --spacing-sm: calc(var(--spacing) * 0.65);
    --spacing-xs: calc(var(--spacing) * 0.3);
}
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body, html {
    min-height: 100vh;
    box-sizing: border-box;
    padding: 0;
    margin: 0;

    background-color: var(--background);
}

h1,h2,h3,h4,h5,h6,label[for] {
    font-family: Poppins;
    font-weight: 600;
}

a, p, span {
    font-family: Lato;
    font-size: 20px;
    text-decoration: none;
    color: var(--text-color);
}

h1 {
    font-size: 2.4rem;
}

h2 {
    font-size: 1.6rem;
}

h3, label[for] {
    font-size: 1.4rem;
}

h4, h5, h6 {
    font-size: 1rem;
}

.content-padding {
    padding: 0 10vw;
}

@media (min-width: 1280px) {
    .content-padding {
        padding: 0 20vw;
    }
}

.inline-flex {
    display: inline-flex;
}

.flex {
    display: flex;
    &.column {
        flex-direction: column;
    }
}

.flex-center {
    justify-content: center;
    align-items: center;
}

.flex.grow, .flex .grow {
    flex-grow: 1;
}

.align-stretch {
    align-items: stretch;
}

.justify-center {
    justify-content: center;
}

.align-center {
    align-items: center;
}

.align-start {
    align-items: start;
}

.justify-between {
    justify-content: space-between;
}

.justify-right {
    justify-content: right;
}

.text-center {
    text-align: center;
}

.pill {
    padding: 0.2em 0.5em;
    border-radius: 0.3em;
    &.secondary {
        background: #dbdbdb;
    }
}

.muted {
    color: rgb(80, 80, 80);
    font-weight: 300;
    @media (prefers-color-scheme: dark) {
        color: rgb(150,150,150);
    }
}

button, .button {
    appearance: none;
    padding: 0.4em 0.65em;
    border-radius: 0.3em;
    border: none;
    cursor: pointer;
    background: transparent;
    color: var(--text-color);

    &.lg {
        font-size: 20px;
    }

    &.md {
        font-size: 16px;
    }

    &.primary {
        background: var(--primary);
        color: var(--text-color);
    }
}

a {
    color: var(--primary);
}

p:after {
    content:"";
    display:inline-block;
    width:0px;
}

@media (prefers-color-scheme: dark) {
    :root {
        color: white;
         --background: #0b0b0b;
         --text-color: #e4e3e6;
    }
}

ul, li {
    list-style-position: inside;
}

hr {
    appearance: none;
    width: 100%;
    border: none;
    border-top: 1px solid white;
    margin: 0.5em 0;
}

.tooltip {
    position: relative;
    border-bottom: 2px dotted var(--primary);

    & [role="tooltip-text"] {
        visibility: hidden;
        min-width: 120px;
        width: max-content;
        background-color: black;
        color: #fff;
        text-align: center;
        border-radius: 0.3em;
        font-size: 16px;
        font-family: Lato;
        font-weight: 400;
        padding: 0.3em 0.5em;
        position: absolute;
        z-index: 1;
        top: -5px;
        left: 110%;
        &:after {
            content: "";
            position: absolute;
            top: 50%;
            right: 100%;
            margin-top: -5px;
            border-width: 5px;
            border-style: solid;
            border-color: transparent black transparent transparent;
        }
    }

    & [role="tooltip-content"] {
        position: absolute;
        z-index: 1;
        visibility: hidden;
        font-size: 14px;
        font-weight: 400;
        width: max-content;
        min-width: 150px;
        background: color-mix(in lch, var(--background) 95%, white);
        padding: 0.7em 1em;
        border-radius: 0.3em;
        box-shadow: 0px 0px 20px -4px var(--other);

        &, &.right {
            top: -5px;
            left: 110%;
        }

        & :is(span, p, a) { font-size: inherit; }
    }
    &:hover {
        cursor: help;
    }

    &:hover :is([role="tooltip-text"],[role="tooltip-content"]) {
        visibility: visible;
    }
}

button {
    transition: background 250ms ease-in-out;
    &.primary-10 {
        background: var(--primary-10);
    }
    &.primary-20 {
        background: var(--primary-20);
    }
    &.primary-30 {
        background: var(--primary-30);
    }
    &.primary-40 {
        background: var(--primary-40);
    }
    &.primary-50 {
        background: var(--primary-50);
    }
    &.primary {
        background: var(--primary);
    }
    &.primary-200 {
        background: var(--primary-200);
    }
    &.secondary {
        background: var(--secondary);
    }
    &.accent {
        background: var(--accent);
        color: black;
    }
    &.big {
        font-size: 18px;
    }
    &:hover {
        background: var(--primary-200) !important;
    }
}

.h-full {
    height: 100%;
}

.w-full {
    width: 100%;
}

.w-auto {
    width: auto;
}

.block {
    display: block;
}

:is(ul, li)[role="navigation"] {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    margin-bottom: 1em;
    list-style: none;
    li {
        transition: background 300ms;
        border-bottom: 2px solid transparent;
        color: var(--text-color);
        cursor: pointer;
        border-bottom: 2px solid var(--background-300);
        &:hover {
            background: var(--background-200);
        }
        & a {
            color: inherit;
            padding: 0.6em;
            display: block;
        }
        &.active {
            border-bottom: 2px solid var(--primary);
            font-weight: bold;
        }
    }
}

.cover img {
    width: 100%;
    height: 100%;
    object-fit: cover;
}

.hidden {
    visibility: hidden;
}

.full-hidden {
    display: none
}

.rounded {
    border-radius: 0.6em;
}

.rounded-sm {
    border-radius: 0.2em;
}

.mt {
    margin-top: var(--spacing);
}
.mt-lg {
    margin-top: var(--spacing-lg);
}
.mt-sm {
    margin-top: var(--spacing-sm);
}

.mb {
    margin-bottom: var(--spacing);
}
.mb-lg {
    margin-bottom: var(--spacing-lg);
}
.mb-sm {
    margin-bottom: var(--spacing-sm);
}

.mr {
    margin-right: var(--spacing);
}
.mr-lg {
    margin-right: var(--spacing-lg);
}
.mr-sm {
    margin-right: var(--spacing-sm);
}


.ml {
    margin-left: var(--spacing);
}
.ml-lg {
    margin-left: var(--spacing-lg);
}
.ml-sm {
    margin-left: var(--spacing-sm);
}

textarea:not(.transparent), input[type="text"]:not(.transparent), input[type="password"]:not(.transparent), select:not(.transparent), .mimic-input {
    width: 100%;
    padding: 0.3em 0.5em;
    border: none;
    background: var(--background-800);
    outline: 1px solid var(--other);
    outline-offset: 1px;
    margin: 3px 0;
    border-radius: 0.3em;
    border: none;
    font-size: 16px;
    transition: all 100ms;
    &:focus-within {
        outline: 2px solid var(--primary-300);
        outline-offset: 2px;
    }
}

input.transparent {
    appearance: none;
    border: none;
    background: transparent;
    font-size: inherit;
    &:focus {
        border: none;
        outline: none;
    }
}

:is(h1, h2, h3, h4, h5, h6) + :is(textarea, input[type="text"], select) {
    margin: 0.3em 0 0.5em 0;
}

.text-sm {
    font-size: 16px;
}

.text-md {
    font-size: 20px;
}

.text-lg {
    font-size: 24px;
}

.gap {
    gap: var(--spacing);
}

.gap-sm {
    gap: var(--spacing-sm);
}

.gap-xs {
    gap: var(--spacing-xs);
}

.gap-lg {
    gap: var(--spacing-lg);
}

.p {
    padding: var(--spacing);
}

.p-sm {
    padding: var(--spacing-sm);
}

.p-lg {
    padding: var(--spacing-lg);
}

.grid {
    display: grid;
    grid-template-columns: var(--columns);
}

.inline-block {
    display: inline-block;
}

.circle {
    border-radius: 9999px;
}

[x-cloak] { visibility: hidden; }

.absolute {
    position: absolute;
}

.relative {
    position: relative;
}

.card {
  padding: var(--spacing) var(--spacing-lg);
  border-radius: 15px;
  border: 1px solid var(--background-200);
  background: var(--background);
  color: var(--text-color);
}

:is(span, p).big {
font-size: 36px;
font-family: Poppins;
}

)";
